#include "tinynet/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "tinynet/Channel.h"

namespace tinynet {

namespace {

thread_local EventLoop* t_loop_in_this_thread = nullptr;

constexpr int kPollTimeoutMs = 10000;

int create_eventfd() {
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "eventfd");
    }
    return fd;
}

}  // namespace

EventLoop* EventLoop::current() noexcept { return t_loop_in_this_thread; }

EventLoop::EventLoop()
    : thread_id_(std::this_thread::get_id()),
      poller_(Poller::new_default_poller(this)),
      wakeup_fd_(create_eventfd()),
      wakeup_channel_(std::make_unique<Channel>(this, wakeup_fd_)) {
    if (t_loop_in_this_thread != nullptr) {
        std::fprintf(stderr, "Another EventLoop already exists in this thread\n");
        std::abort();
    }
    t_loop_in_this_thread = this;
    wakeup_channel_->set_read_callback([this] { handle_wakeup(); });
    wakeup_channel_->enable_reading();
    timer_queue_ = std::make_unique<TimerQueue>(this);
}

EventLoop::~EventLoop() {
    timer_queue_.reset();
    wakeup_channel_->disable_all();
    wakeup_channel_->remove();
    ::close(wakeup_fd_);
    t_loop_in_this_thread = nullptr;
}

void EventLoop::loop() {
    assert(!looping_);
    assert_in_loop_thread();
    looping_ = true;
    // 注意：不能在進入時 quit_ = false。EventLoopThread 的析構可能在 loop() 開始「之前」
    // 就呼叫 quit()；若在這裡重設，那次 quit 就被吃掉，loop 永遠不會結束，join() 卡死。
    // 改在離開時重設，讓同一個 loop 之後還能再跑一次。

    while (!quit_) {
        active_channels_.clear();
        poller_->poll(kPollTimeoutMs, &active_channels_);
        for (Channel* channel : active_channels_) {
            channel->handle_event();
        }
        do_pending_functors();
    }
    quit_ = false;
    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    // 在其他執行緒呼叫時，loop 可能正卡在 epoll_wait，要叫醒它才會檢查 quit_
    if (!is_in_loop_thread()) {
        wakeup();
    }
}

void EventLoop::run_in_loop(Functor cb) {
    if (is_in_loop_thread()) {
        cb();
    } else {
        queue_in_loop(std::move(cb));
    }
}

void EventLoop::queue_in_loop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_functors_.push_back(std::move(cb));
    }
    // 需要喚醒的兩種情況：
    // 1. 呼叫者不在 loop 執行緒：loop 可能正睡在 epoll_wait
    // 2. loop 正在執行 pending functors，而某個 functor 又排入新工作：
    //    這批新工作不在本輪交換出來的清單裡，要讓下一輪 epoll_wait 立刻返回
    if (!is_in_loop_thread() || calling_pending_functors_) {
        wakeup();
    }
}

TimerId EventLoop::run_at(TimePoint when, TimerCallback cb) {
    return timer_queue_->add_timer(std::move(cb), when, Duration::zero());
}

TimerId EventLoop::run_after(Duration delay, TimerCallback cb) {
    return run_at(Clock::now() + delay, std::move(cb));
}

TimerId EventLoop::run_every(Duration interval, TimerCallback cb) {
    return timer_queue_->add_timer(std::move(cb), Clock::now() + interval, interval);
}

void EventLoop::cancel(TimerId timer_id) { timer_queue_->cancel(timer_id); }

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        std::fprintf(stderr, "EventLoop::wakeup() wrote %zd bytes\n", n);
    }
}

void EventLoop::handle_wakeup() {
    uint64_t value = 0;
    ssize_t n = ::read(wakeup_fd_, &value, sizeof(value));
    if (n != sizeof(value)) {
        std::fprintf(stderr, "EventLoop::handle_wakeup() read %zd bytes\n", n);
    }
}

void EventLoop::update_channel(Channel* channel) {
    assert(channel->owner_loop() == this);
    assert_in_loop_thread();
    poller_->update_channel(channel);
}

void EventLoop::remove_channel(Channel* channel) {
    assert(channel->owner_loop() == this);
    assert_in_loop_thread();
    poller_->remove_channel(channel);
}

bool EventLoop::has_channel(const Channel* channel) const {
    assert(channel->owner_loop() == this);
    return poller_->has_channel(channel);
}

void EventLoop::assert_in_loop_thread() const {
    if (!is_in_loop_thread()) {
        std::fprintf(stderr, "EventLoop used from a thread other than its owner\n");
        std::abort();
    }
}

void EventLoop::do_pending_functors() {
    std::vector<Functor> functors;
    calling_pending_functors_ = true;
    {
        // 只在交換時持鎖：縮短臨界區，也避免 functor 內再呼叫 queue_in_loop 時死鎖
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pending_functors_);
    }
    for (const Functor& functor : functors) {
        functor();
    }
    calling_pending_functors_ = false;
}

}  // namespace tinynet
