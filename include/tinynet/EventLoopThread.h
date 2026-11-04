#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace tinynet {

class EventLoop;

// 啟動一條新執行緒，在裡面建立並執行一個 EventLoop。
class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    explicit EventLoopThread(ThreadInitCallback cb = {}, std::string name = {});
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    // 啟動執行緒，並等到裡面的 EventLoop 建好才回傳它的指標
    EventLoop* start_loop();

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    void thread_func();

    EventLoop* loop_{nullptr};  // 由 mutex_ 保護
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback init_cb_;
    std::string name_;
};

}  // namespace tinynet
