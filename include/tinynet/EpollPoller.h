#pragma once

#include <sys/epoll.h>

#include <vector>

#include "tinynet/Poller.h"

namespace tinynet {

// 以 epoll 實作 Poller，使用水平觸發 (LT)。
//
// 為什麼選 LT 而不是 ET？
//   - LT：只要 fd 還可讀/可寫就會一直通知。一次沒讀完也不會漏事件，程式較不易出錯。
//   - ET：只在狀態「改變」時通知一次，必須一路讀/寫到 EAGAIN，否則資料會卡住。
//   本專案的 Buffer::read_fd 用 readv + 64KB 額外緩衝，一次系統呼叫就能讀很多資料，
//   LT 的額外 epoll_wait 次數影響不大，換來的是更簡單、更不容易出錯的程式。
class EpollPoller final : public Poller {
public:
    explicit EpollPoller(EventLoop* loop);
    ~EpollPoller() override;

    void poll(int timeout_ms, ChannelList* active_channels) override;
    void update_channel(Channel* channel) override;
    void remove_channel(Channel* channel) override;

private:
    static constexpr int kInitEventListSize = 16;

    void update(int operation, Channel* channel);

    int epollfd_;
    std::vector<epoll_event> events_;
};

}  // namespace tinynet
