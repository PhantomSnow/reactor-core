#include "tinynet/EpollPoller.h"

#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "tinynet/Channel.h"

namespace tinynet {

namespace {
// Channel::index() 在 EpollPoller 中的意義
constexpr int kNew = -1;     // 從未加入 epoll
constexpr int kAdded = 1;    // 目前在 epoll 中
constexpr int kDeleted = 2;  // 曾加入、暫時從 epoll 移除（不關心任何事件），仍留在 channels_
}  // namespace

EpollPoller::EpollPoller(EventLoop* loop)
    : Poller(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitEventListSize) {
    if (epollfd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

EpollPoller::~EpollPoller() { ::close(epollfd_); }

void EpollPoller::poll(int timeout_ms, ChannelList* active_channels) {
    const int n = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeout_ms);
    if (n < 0) {
        if (errno != EINTR) {  // 被訊號打斷不是錯誤，下一輪再等就好
            std::perror("epoll_wait");
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        active_channels->push_back(channel);
    }
    // 事件陣列被塞滿，代表活躍連線可能更多，下次擴大
    if (static_cast<std::size_t>(n) == events_.size()) {
        events_.resize(events_.size() * 2);
    }
}

void EpollPoller::update_channel(Channel* channel) {
    const int index = channel->index();
    const int fd = channel->fd();
    if (index == kNew || index == kDeleted) {
        if (index == kNew) {
            assert(channels_.find(fd) == channels_.end());
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    } else {
        assert(has_channel(channel));
        if (channel->is_none_event()) {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        } else {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::remove_channel(Channel* channel) {
    assert(has_channel(channel));
    assert(channel->is_none_event());
    channels_.erase(channel->fd());
    if (channel->index() == kAdded) {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

void EpollPoller::update(int operation, Channel* channel) {
    epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;  // 直接存 Channel*，事件回來時不必再查表
    if (::epoll_ctl(epollfd_, operation, channel->fd(), &event) < 0) {
        if (operation == EPOLL_CTL_DEL) {
            std::fprintf(stderr, "epoll_ctl DEL fd=%d: %s\n", channel->fd(), std::strerror(errno));
        } else {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl");
        }
    }
}

}  // namespace tinynet
