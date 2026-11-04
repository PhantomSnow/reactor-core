#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "tinynet/Poller.h"
#include "tinynet/Timer.h"

namespace tinynet {

class Channel;

// Reactor 的核心：one loop per thread。
//   while (!quit) {
//       poller->poll(...)              // 等待 I/O 事件 / 計時器 / 喚醒
//       for ch in active: ch->handle_event()
//       do_pending_functors()          // 執行其他執行緒丟進來的工作
//   }
// EventLoop 物件只能在建立它的執行緒中執行；其他執行緒要它做事，
// 一律透過 run_in_loop / queue_in_loop 把工作丟進來，再用 eventfd 喚醒它。
class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit();  // thread-safe

    // 在 loop 執行緒中：立即執行；在其他執行緒中：排入佇列並喚醒 loop
    void run_in_loop(Functor cb);
    // 一律排入佇列，等這一輪事件處理完才執行
    void queue_in_loop(Functor cb);

    // 計時器（thread-safe）
    TimerId run_at(TimePoint when, TimerCallback cb);
    TimerId run_after(Duration delay, TimerCallback cb);
    TimerId run_every(Duration interval, TimerCallback cb);
    void cancel(TimerId timer_id);

    void wakeup();

    // 只給 Channel 呼叫
    void update_channel(Channel* channel);
    void remove_channel(Channel* channel);
    bool has_channel(const Channel* channel) const;

    [[nodiscard]] bool is_in_loop_thread() const noexcept {
        return thread_id_ == std::this_thread::get_id();
    }
    void assert_in_loop_thread() const;

    // 目前執行緒擁有的 EventLoop（沒有則為 nullptr）
    static EventLoop* current() noexcept;

private:
    void handle_wakeup();
    void do_pending_functors();

    std::atomic<bool> looping_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> calling_pending_functors_{false};
    const std::thread::id thread_id_;

    // 宣告順序很重要：timer_queue_ 與 wakeup_channel_ 建構時會向 poller_ 註冊
    std::unique_ptr<Poller> poller_;
    const int wakeup_fd_;
    std::unique_ptr<Channel> wakeup_channel_;
    std::unique_ptr<TimerQueue> timer_queue_;

    Poller::ChannelList active_channels_;

    std::mutex mutex_;
    std::vector<Functor> pending_functors_;  // 由 mutex_ 保護
};

}  // namespace tinynet
