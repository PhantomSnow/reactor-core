#include "tinynet/EventLoopThreadPool.h"

#include <cassert>

#include "tinynet/EventLoop.h"
#include "tinynet/EventLoopThread.h"

namespace tinynet {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop, std::string name)
    : base_loop_(base_loop), name_(std::move(name)) {}

// 各個 EventLoop 是 EventLoopThread 執行緒堆疊上的物件，不需要在這裡 delete
EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::start(const ThreadInitCallback& cb) {
    assert(!started_);
    base_loop_->assert_in_loop_thread();
    started_ = true;
    for (int i = 0; i < num_threads_; ++i) {
        auto thread = std::make_unique<EventLoopThread>(cb, name_ + "-io" + std::to_string(i));
        loops_.push_back(thread->start_loop());
        threads_.push_back(std::move(thread));
    }
    if (num_threads_ == 0 && cb) {
        cb(base_loop_);
    }
}

EventLoop* EventLoopThreadPool::get_next_loop() {
    base_loop_->assert_in_loop_thread();
    if (loops_.empty()) {
        return base_loop_;
    }
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::get_all_loops() {
    base_loop_->assert_in_loop_thread();
    if (loops_.empty()) {
        return {base_loop_};
    }
    return loops_;
}

}  // namespace tinynet
