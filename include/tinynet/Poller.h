#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

namespace tinynet {

class Channel;
class EventLoop;

// I/O 多工的抽象介面。EventLoop 只依賴這個介面，不知道底層是 epoll；
// 要換成 poll / kqueue 只需要新增一個子類別（open-closed principle）。
class Poller {
public:
    using ChannelList = std::vector<Channel*>;

    explicit Poller(EventLoop* loop) noexcept : owner_loop_(loop) {}
    virtual ~Poller() = default;

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    // 等待事件，把就緒的 Channel 填進 active_channels。必須在 loop 執行緒呼叫。
    virtual void poll(int timeout_ms, ChannelList* active_channels) = 0;
    virtual void update_channel(Channel* channel) = 0;
    virtual void remove_channel(Channel* channel) = 0;

    [[nodiscard]] bool has_channel(const Channel* channel) const;

    static std::unique_ptr<Poller> new_default_poller(EventLoop* loop);

protected:
    std::unordered_map<int, Channel*> channels_;  // fd -> Channel
    EventLoop* owner_loop_;
};

}  // namespace tinynet
