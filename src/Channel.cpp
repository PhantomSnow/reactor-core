#include "tinynet/Channel.h"

#include <sys/epoll.h>

#include <cassert>

#include "tinynet/EventLoop.h"

namespace tinynet {

const uint32_t Channel::kNoneEvent = 0;
const uint32_t Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const uint32_t Channel::kWriteEvent = EPOLLOUT;

Channel::~Channel() {
    assert(!event_handling_);
    assert(!added_to_loop_ && "Channel destroyed while still registered in Poller");
}

void Channel::tie(const std::shared_ptr<void>& owner) {
    tie_ = owner;
    tied_ = true;
}

void Channel::update() {
    added_to_loop_ = true;
    loop_->update_channel(this);
}

void Channel::remove() {
    assert(is_none_event());
    added_to_loop_ = false;
    loop_->remove_channel(this);
}

void Channel::handle_event() {
    if (tied_) {
        // 擁有者已經死了就不處理；活著的話 guard 會延長它的生命到本函式結束
        std::shared_ptr<void> guard = tie_.lock();
        if (guard) {
            handle_event_with_guard();
        }
    } else {
        handle_event_with_guard();
    }
}

void Channel::handle_event_with_guard() {
    event_handling_ = true;
    // 對端關閉且沒有資料可讀 → 關閉；有資料就先讓 read 回呼把資料讀完（read 回傳 0 時再關）
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (close_cb_) close_cb_();
    }
    if (revents_ & EPOLLERR) {
        if (error_cb_) error_cb_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (read_cb_) read_cb_();
    }
    if (revents_ & EPOLLOUT) {
        if (write_cb_) write_cb_();
    }
    event_handling_ = false;
}

}  // namespace tinynet
