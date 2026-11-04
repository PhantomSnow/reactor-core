#include "tinynet/Timer.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <system_error>

#include "tinynet/EventLoop.h"

namespace tinynet {

std::atomic<int64_t> Timer::next_sequence_{0};

namespace {

int create_timerfd() {
    int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "timerfd_create");
    }
    return fd;
}

}  // namespace

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop), timerfd_(create_timerfd()), timerfd_channel_(loop, timerfd_) {
    timerfd_channel_.set_read_callback([this] { handle_read(); });
    timerfd_channel_.enable_reading();
}

TimerQueue::~TimerQueue() {
    timerfd_channel_.disable_all();
    timerfd_channel_.remove();
    ::close(timerfd_);
    for (const Entry& entry : timers_) {
        delete entry.second;
    }
}

TimerId TimerQueue::add_timer(TimerCallback cb, TimePoint when, Duration interval) {
    auto* timer = new Timer(std::move(cb), when, interval);
    // 必須在交給 loop 執行緒「之前」組好 TimerId：一旦 run_in_loop 把 timer 交出去，
    // loop 執行緒可能立刻觸發並 delete 它（短的一次性計時器），之後再讀 timer->sequence()
    // 就是 use-after-free。這個 race 是用 ThreadSanitizer 抓到的。
    const TimerId id{timer, timer->sequence()};
    loop_->run_in_loop([this, timer] { add_timer_in_loop(timer); });
    return id;
}

void TimerQueue::cancel(TimerId timer_id) {
    loop_->run_in_loop([this, timer_id] { cancel_in_loop(timer_id); });
}

void TimerQueue::add_timer_in_loop(Timer* timer) {
    loop_->assert_in_loop_thread();
    if (insert(timer)) {
        reset_timerfd(timer->expiration());
    }
}

void TimerQueue::cancel_in_loop(TimerId timer_id) {
    loop_->assert_in_loop_thread();
    ActiveTimer key{timer_id.timer, timer_id.sequence};
    auto it = active_timers_.find(key);
    if (it != active_timers_.end()) {
        timers_.erase(Entry{it->first->expiration(), it->first});
        delete it->first;
        active_timers_.erase(it);
    } else if (calling_expired_timers_) {
        // 這個計時器已經從 timers_ 取出、正在執行回呼；記下來，reset() 時不要重新排程
        canceling_timers_.insert(key);
    }
}

void TimerQueue::handle_read() {
    loop_->assert_in_loop_thread();
    uint64_t howmany = 0;
    // 必須把 timerfd 讀掉，否則 LT 模式下會一直觸發
    [[maybe_unused]] ssize_t n = ::read(timerfd_, &howmany, sizeof(howmany));

    const TimePoint now = Clock::now();
    std::vector<Entry> expired = get_expired(now);

    calling_expired_timers_ = true;
    canceling_timers_.clear();
    for (const Entry& entry : expired) {
        entry.second->run();
    }
    calling_expired_timers_ = false;

    reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::get_expired(TimePoint now) {
    // 哨兵：到期時間 = now、指標取最大值，lower_bound 會停在第一個「尚未到期」的計時器
    const Entry sentry{now, reinterpret_cast<Timer*>(UINTPTR_MAX)};
    auto end = timers_.lower_bound(sentry);
    std::vector<Entry> expired(timers_.begin(), end);
    timers_.erase(timers_.begin(), end);
    for (const Entry& entry : expired) {
        active_timers_.erase(ActiveTimer{entry.second, entry.second->sequence()});
    }
    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, TimePoint now) {
    for (const Entry& entry : expired) {
        Timer* timer = entry.second;
        ActiveTimer key{timer, timer->sequence()};
        if (timer->repeat() && canceling_timers_.find(key) == canceling_timers_.end()) {
            timer->restart(now);
            insert(timer);
        } else {
            delete timer;
        }
    }
    if (!timers_.empty()) {
        reset_timerfd(timers_.begin()->first);
    }
}

bool TimerQueue::insert(Timer* timer) {
    const TimePoint when = timer->expiration();
    const bool earliest_changed = timers_.empty() || when < timers_.begin()->first;
    timers_.insert(Entry{when, timer});
    active_timers_.insert(ActiveTimer{timer, timer->sequence()});
    return earliest_changed;
}

void TimerQueue::reset_timerfd(TimePoint expiration) {
    using namespace std::chrono;
    auto delta = duration_cast<microseconds>(expiration - Clock::now());
    if (delta < microseconds(100)) {
        delta = microseconds(100);  // 0 會讓 timerfd 解除設定，所以給個最小值
    }
    itimerspec new_value{};
    new_value.it_value.tv_sec = static_cast<time_t>(delta.count() / 1000000);
    new_value.it_value.tv_nsec = static_cast<long>((delta.count() % 1000000) * 1000);
    ::timerfd_settime(timerfd_, 0, &new_value, nullptr);
}

}  // namespace tinynet
