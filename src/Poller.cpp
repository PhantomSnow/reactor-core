#include "tinynet/Poller.h"

#include "tinynet/Channel.h"
#include "tinynet/EpollPoller.h"

namespace tinynet {

bool Poller::has_channel(const Channel* channel) const {
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

std::unique_ptr<Poller> Poller::new_default_poller(EventLoop* loop) {
    return std::make_unique<EpollPoller>(loop);
}

}  // namespace tinynet
