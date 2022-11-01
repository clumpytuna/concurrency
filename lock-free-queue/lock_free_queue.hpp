#ifndef lock_free_queue_hpp
#define lock_free_queue_hpp

#include <atomic>
#include <memory>
#include <thread>


template <typename T, template <typename U> class Atomic = std::atomic>
class LockFreeQueue {
  struct Node {
    T element_{};
    Atomic<Node*> next_{nullptr};
    
    explicit Node(T element, Node* next = nullptr)
        : element_(std::move(element)),
          next_(next) {
    }
    
    explicit Node() { }
  };
  
public:
  explicit LockFreeQueue() {
    Node* dummy =  new Node{};
    head_ = dummy;
    tail_ = dummy;
    garbage_head_ = dummy;
  }
  
  ~LockFreeQueue() {
    DeleteGarbage(nullptr);
  }
  
  void Enqueue(T element) {
    
    ++operations_counter_;
    
    auto new_tail = new Node{element};
    auto curr_tail = tail_.load();
    
    while (true) {
      
      curr_tail = tail_.load();
      Node* null = nullptr;
      
      if (!curr_tail->next_.load()) {
        if (curr_tail->next_.compare_exchange_strong(null, new_tail)) {
          break;
        }
      } else {
        tail_.compare_exchange_strong(curr_tail, curr_tail->next_.load());
      }
    }
    
    tail_.compare_exchange_strong(curr_tail, new_tail); // 2
    --operations_counter_;
    
  }
  
  bool Dequeue(T& element) {
    ++operations_counter_;
    
    while (true) {
      
      auto curr_head = head_.load();
      auto curr_tail = tail_.load();
      
      if (curr_head == curr_tail) {
        if (curr_head->next_.load() == nullptr) {
          return false;
        } else {
          tail_.compare_exchange_strong(curr_head, curr_head->next_.load());
        }
      } else {
        if (head_.compare_exchange_strong(curr_head, curr_head->next_.load())) {
          element = curr_head->next_.load()->element_;
          curr_head = head_.load();
          if (operations_counter_.load() == 1) {
            DeleteGarbage(curr_head);
            --operations_counter_;
          } else {
            --operations_counter_;
          }
          return true;
        }
      }
    }
  }
  
private:
  enum class garbage_sources {GARBAGE_HEAD, HEAD};
  Atomic<Node*> head_{nullptr};
  Atomic<Node*> tail_{nullptr};
  Atomic<Node*> garbage_head_{nullptr};
  Atomic<size_t> operations_counter_{0};
  
  void DeleteGarbage(Node *head) {
    while (garbage_head_.load() != head) {
      auto garbage = garbage_head_.load();
      garbage_head_.store(garbage_head_.load()->next_.load());
      delete garbage;
    }
  }
};

///////////////////////////////////////////////////////////////////////

#endif