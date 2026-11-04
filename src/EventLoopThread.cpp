#include "tinynet/EventLoopThread.h"

#include <pthread.h>

#include "tinynet/EventLoop.h"

namespace tinynet {

EventLoopThread::EventLoopThread(ThreadInitCallback cb, std::string name)
    : init_cb_(std::move(cb)), name_(std::move(name)) {}

EventLoopThread::~EventLoopThread() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_ != nullptr) {
            loop_->quit();
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

EventLoop* EventLoopThread::start_loop() {
    thread_ = std::thread([this] { thread_func(); });
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });
    return loop_;
}

void EventLoopThread::thread_func() {
    if (!name_.empty()) {
        // Linux 執行緒名稱上限 15 字元，方便用 top -H / gdb 觀察
        ::pthread_setname_np(::pthread_self(), name_.substr(0, 15).c_str());
    }
    EventLoop loop;  // 生命週期 = 這條執行緒的生命週期
    if (init_cb_) {
        init_cb_(&loop);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    cond_.notify_one();

    loop.loop();

    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}

}  // namespace tinynet
