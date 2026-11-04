#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tinynet {

class EventLoop;
class EventLoopThread;

// 一個 base loop（負責 accept）+ N 個 I/O loop（負責讀寫連線）。
// 新連線以 round-robin 分配給 I/O loop；N = 0 時所有工作都在 base loop 上做。
class EventLoopThreadPool {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThreadPool(EventLoop* base_loop, std::string name);
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void set_thread_num(int num_threads) noexcept { num_threads_ = num_threads; }
    void start(const ThreadInitCallback& cb = {});

    // 只能在 base loop 執行緒呼叫
    EventLoop* get_next_loop();
    std::vector<EventLoop*> get_all_loops();

    [[nodiscard]] bool started() const noexcept { return started_; }

private:
    EventLoop* base_loop_;
    std::string name_;
    bool started_{false};
    int num_threads_{0};
    std::size_t next_{0};
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

}  // namespace tinynet
