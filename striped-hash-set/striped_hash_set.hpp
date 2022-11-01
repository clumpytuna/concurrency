//
//  stiped-hash-set.hpp
//  Concurrency
//

#ifndef stiped_hash_set_hpp
#define stiped_hash_set_hpp

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <forward_list>
#include <mutex>
#include <shared_mutex>
#include <vector>

class ReadWriteMutex {
public:
  ReadWriteMutex()
      : readers_(0),
        writers_(0),
        writing_(false) {};
  
  ReadWriteMutex(const ReadWriteMutex&) = delete;
  
  ReadWriteMutex(ReadWriteMutex&&) = delete;
  
  void lock() {
    std::unique_lock<std::mutex> lock(lock_);
    ++writers_;
    while (writing_ || readers_) {
      unlocked_.wait(lock);
    }
    writing_ = true;
  }
  
  void lock_shared() {
    std::unique_lock<std::mutex> lock(lock_);
    while (writing_ || writers_) {
      unlocked_.wait(lock);
    }
    ++readers_;
  }
  
  void unlock() {
    std::unique_lock<std::mutex> lock(lock_);
    writing_ = false;
    --writers_;
    unlocked_.notify_all();
  }
  
  void unlock_shared() {
    std::unique_lock<std::mutex> lock(lock_);
    --readers_;
    if (readers_ == 0 and writers_) {
      unlocked_.notify_all();
    }
  }
  
private:
  std::mutex lock_;
  std::condition_variable unlocked_;
  size_t readers_;
  size_t writers_;
  bool writing_;
};


template <typename T, class Hash = std::hash<T>>
class StripedHashSet {
private:
  using WriterLock = std::unique_lock<ReadWriteMutex>;
  using ReaderLock = std::shared_lock<ReadWriteMutex>;
  
public:
  explicit StripedHashSet(const size_t concurrency_level,
                          const size_t growth_factor = 3,
                          const double load_factor = 0.75)
      : concurrency_level_(concurrency_level),
        growth_factor_(growth_factor),
        max_load_factor_(load_factor),
        locks_(concurrency_level),
        buckets_(concurrency_level * growth_factor),
        num_elements_(0) {};
  
  bool Insert(const T& element) {
    const size_t hash = GetHash(element);
    
    auto lock = WriteLockBucket(hash);
    
    const size_t bucket_index = GetBucketIndex(hash);
    
    if (ContainsInBucket(bucket_index, element)) {
      return false;
    }
    
    buckets_[bucket_index].push_front(element);
    num_elements_.fetch_add(1);
    
    if (NeedToResize()) {
      lock.unlock();
      Resize();
    }
    
    return true;
  }
  
  bool Remove(const T& element) {
    const size_t hash = GetHash(element);
    
    auto lock = WriteLockBucket(hash);
    
    const size_t bucket_index = GetBucketIndex(hash);
    
    if (!ContainsInBucket(bucket_index, element)) {
      return false;
    }
    
    buckets_[bucket_index].remove(element);
    num_elements_.fetch_sub(1);
    
    return true;
  }
  
  bool Contains(const T& element) const {
    const size_t hash = GetHash(element);
    
    auto lock = ReadLockBucket(hash);
    
    const size_t bucket_index = GetBucketIndex(hash);
    
    return ContainsInBucket(bucket_index, element);
  }
  
  size_t Size() const {
    return num_elements_;
  }
  
private:
  
  WriterLock WriteLockBucket(const size_t element_hash) const {
    return WriterLock(locks_[GetStripeIndex(element_hash)]);
  }
  
  ReaderLock ReadLockBucket(const size_t element_hash) const {
    return ReaderLock(locks_[GetStripeIndex(element_hash)]);
  }
  
  bool ContainsInBucket(const size_t bucket_index, const T& element) const {
    return std::find(
               buckets_[bucket_index].begin(),
               buckets_[bucket_index].end(),
               element) != buckets_[bucket_index].end();
  }
  
  size_t GetBucketIndex(const size_t element_hash,
                        const size_t num_buckets) const {
    return element_hash % num_buckets;
  }
  
  size_t GetBucketIndex(const size_t element_hash) const {
    return GetBucketIndex(element_hash, buckets_.size());
  }
  
  size_t GetStripeIndex(const size_t element_hash) const {
    return element_hash % concurrency_level_;
  }
  
  double GetLoadFactor() const {
    return (double)num_elements_ / buckets_.size();
  }
  
  size_t GetHash(const T& element) const {
    return hasher_(element);
  }
  
  bool NeedToResize() const {
    return GetLoadFactor() >= max_load_factor_;
  }
  
  void Resize() {
    std::vector<WriterLock> locks;
    
    locks.emplace_back(locks_[0]);
    
    if (!NeedToResize()) {
      return;
    }
    
    for (size_t i = 1; i < concurrency_level_; ++i) {
      locks.emplace_back(locks_[i]);
    }
    
    size_t new_buckets_size = buckets_.size() * growth_factor_;
    
    std::vector<std::forward_list<T>> new_buckets(new_buckets_size);
    
    for (auto bucket: buckets_) {
      for (auto& element: bucket) {
        auto hash = GetHash(element);
        new_buckets[GetBucketIndex(hash, new_buckets_size)].push_front(element);
      }
    }
    buckets_.swap(new_buckets);
  }
  
private:
  const size_t concurrency_level_;
  const size_t growth_factor_;
  const double max_load_factor_;
  mutable std::vector<ReadWriteMutex> locks_;
  std::vector< std::forward_list<T>> buckets_;
  std::atomic<size_t> num_elements_;
  Hash hasher_;
};

template <typename T> using ConcurrentSet = StripedHashSet<T>;

#endif /* stiped_hash_set_hpp */