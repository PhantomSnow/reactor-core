#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace tinynet {

class EventLoop;

// Channel = 「一個 fd + 關心的事件 + 事件發生時的回呼」。
// Channel 不擁有 fd（不負責 close），也不知道底層是 epoll 還是 poll；
// 它只負責把 Poller 回報的 revents 分派給對應的回呼。
// 一個 Channel 永遠只屬於一個 EventLoop，只在該 loop 的執行緒裡被操作。
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) noexcept : loop_(loop), fd_(fd) {}
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 由 EventLoop 在 poll 回來之後呼叫
    void handle_event();

    void set_read_callback(EventCallback cb) { read_cb_ = std::move(cb); }
    void set_write_callback(EventCallback cb) { write_cb_ = std::move(cb); }
    void set_close_callback(EventCallback cb) { close_cb_ = std::move(cb); }
    void set_error_callback(EventCallback cb) { error_cb_ = std::move(cb); }

    // 綁定擁有者（例如 TcpConnection）的生命週期。處理事件前先把 weak_ptr
    // lock 成 shared_ptr，保證回呼執行期間擁有者不會被析構。
    void tie(const std::shared_ptr<void>& owner);

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] uint32_t events() const noexcept { return events_; }
    void set_revents(uint32_t revents) noexcept { revents_ = revents; }

    void enable_reading() { events_ |= kReadEvent; update(); }
    void disable_reading() { events_ &= ~kReadEvent; update(); }
    void enable_writing() { events_ |= kWriteEvent; update(); }
    void disable_writing() { events_ &= ~kWriteEvent; update(); }
    void disable_all() { events_ = kNoneEvent; update(); }

    [[nodiscard]] bool is_none_event() const noexcept { return events_ == kNoneEvent; }
    [[nodiscard]] bool is_writing() const noexcept { return (events_ & kWriteEvent) != 0; }
    [[nodiscard]] bool is_reading() const noexcept { return (events_ & kReadEvent) != 0; }

    // Poller 用來記錄這個 Channel 目前在 epoll 裡的狀態
    [[nodiscard]] int index() const noexcept { return index_; }
    void set_index(int idx) noexcept { index_ = idx; }

    [[nodiscard]] EventLoop* owner_loop() const noexcept { return loop_; }

    // 從 Poller 中移除；之後才能安全析構
    void remove();

private:
    static const uint32_t kNoneEvent;
    static const uint32_t kReadEvent;
    static const uint32_t kWriteEvent;

    void update();
    void handle_event_with_guard();

    EventLoop* loop_;
    const int fd_;
    uint32_t events_{0};   // 關心的事件
    uint32_t revents_{0};  // 實際發生的事件
    int index_{-1};

    std::weak_ptr<void> tie_;
    bool tied_{false};
    bool event_handling_{false};
    bool added_to_loop_{false};

    EventCallback read_cb_;
    EventCallback write_cb_;
    EventCallback close_cb_;
    EventCallback error_cb_;
};

}  // namespace tinynet
