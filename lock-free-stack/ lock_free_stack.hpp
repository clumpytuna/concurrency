//
//  lock_free_stack.hpp
//  Concurrency
//

#ifndef lock_free_stack_hpp
#define lock_free_stack_hpp

#include <atomic>
#include <thread>

///////////////////////////////////////////////////////////////////////

template <typename T>
class LockFreeStack {
  struct Node {
    T element_;
    std::atomic<Node*> next_;
    explicit Node(const T& element, Node* next = nullptr)
        : element_(element),
          next_(next) {}
  };

public:
  explicit LockFreeStack() {

  }

  ~LockFreeStack() {
    DeleteList(top_.load());
    DeleteList(garbage_top_.load());
  }

  void Push(T element) {
    auto new_top = new Node(element);
    auto curr_top = top_.load();
    new_top->next_.store(curr_top);
    while (!top_.compare_exchange_weak(curr_top, new_top)) {
      new_top->next_.store(curr_top);
    }
  }

  bool Pop(T& element) {
    auto curr_top = top_.load();
    while (true) {
      if (!curr_top) {
        return false;
      }
      if (top_.compare_exchange_strong(curr_top, curr_top->next_.load())) {
        element = curr_top->element_;
        PushGarbage(curr_top);
        return true;
      }
    }
  }

private:
  std::atomic<Node*> top_{nullptr};
  std::atomic<Node*> garbage_top_{nullptr};

private:
  void DeleteList(Node* garbage) {
    auto curr_garbage = garbage;
    while (garbage != nullptr) {
      curr_garbage = garbage;
      garbage = garbage->next_.load();
      delete curr_garbage;
    }
  }

  void PushGarbage(Node* garbage) {
    auto curr_garbage_top = garbage_top_.load();
    garbage->next_.store(curr_garbage_top);
    while (!garbage_top_.compare_exchange_weak(curr_garbage_top, garbage)) {
      garbage->next_.store(curr_garbage_top);
    }
  }
};

///////////////////////////////////////////////////////////////////////

template <typename T>
using ConcurrentStack = LockFreeStack<T>;

///////////////////////////////////////////////////////////////////////

#endif /* lock_free_stack_hpp */

