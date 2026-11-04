#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

#include "tinynet/Channel.h"

namespace tinynet {

class EventLoop;

using Clock = std::chrono::steady_clock;  // 單調時鐘：不受系統調時間影響
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
using TimerCallback = std::function<void()>;

class Timer {
public:
    Timer(TimerCallback cb, TimePoint when, Duration interval)
        : callback_(std::move(cb)),
          expiration_(when),
          interval_(interval),
          sequence_(next_sequence_.fetch_add(1) + 1) {}

    void run() const { callback_(); }
    void restart(TimePoint now) { expiration_ = now + interval_; }

    [[nodiscard]] TimePoint expiration() const noexcept { return expiration_; }
    [[nodiscard]] bool repeat() const noexcept { return interval_ > Duration::zero(); }
    [[nodiscard]] int64_t sequence() const noexcept { return sequence_; }

private:
    const TimerCallback callback_;
    TimePoint expiration_;
    const Duration interval_;
    const int64_t sequence_;

    static std::atomic<int64_t> next_sequence_;
};

// 用來取消計時器的把手。只有指標不夠：Timer 被刪除後位址可能被新 Timer 重用，
// 所以再配一個全域遞增的 sequence 才能唯一識別。
struct TimerId {
    Timer* timer{nullptr};
    int64_t sequence{0};
};

// 用 timerfd 把「時間到了」變成一個可讀的 fd，接進 epoll。
// 這樣計時器就跟 socket 事件走同一條路徑，EventLoop 不需要特別處理時間。
// 所有計時器依到期時間排序，timerfd 只設成「最早到期的那一個」。
class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 這兩個函式是 thread-safe 的：實際操作會被轉到 loop 執行緒執行
    TimerId add_timer(TimerCallback cb, TimePoint when, Duration interval);
    void cancel(TimerId timer_id);

private:
    using Entry = std::pair<TimePoint, Timer*>;         // 依到期時間排序
    using ActiveTimer = std::pair<Timer*, int64_t>;     // 依位址 + sequence 查找

    void add_timer_in_loop(Timer* timer);
    void cancel_in_loop(TimerId timer_id);
    void handle_read();
    std::vector<Entry> get_expired(TimePoint now);
    void reset(const std::vector<Entry>& expired, TimePoint now);
    bool insert(Timer* timer);
    void reset_timerfd(TimePoint expiration);

    EventLoop* loop_;
    const int timerfd_;
    Channel timerfd_channel_;
    std::set<Entry> timers_;
    std::set<ActiveTimer> active_timers_;

    // 處理「在計時器回呼中取消自己（或其他已到期的計時器）」這種情況
    bool calling_expired_timers_{false};
    std::set<ActiveTimer> canceling_timers_;
};

}  // namespace tinynet
