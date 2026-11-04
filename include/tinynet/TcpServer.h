#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "tinynet/Channel.h"
#include "tinynet/EventLoopThreadPool.h"
#include "tinynet/InetAddress.h"
#include "tinynet/Socket.h"
#include "tinynet/TcpConnection.h"

namespace tinynet {

class EventLoop;

// 多執行緒 TCP server（main reactor + sub reactors）：
//   - base loop：監聽 socket，accept 新連線
//   - I/O loops：由 EventLoopThreadPool 提供，每條連線固定屬於其中一個 loop，
//     之後這條連線的所有讀寫都在同一條執行緒，不需要為連線本身加鎖。
class TcpServer {
public:
    enum class Option { kNoReusePort, kReusePort };

    TcpServer(EventLoop* loop, const InetAddress& listen_addr, std::string name,
              Option option = Option::kNoReusePort);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // 必須在 start() 之前設定。0 = 單執行緒（所有工作都在 base loop）
    void set_thread_num(int num_threads);
    void set_thread_init_callback(EventLoopThreadPool::ThreadInitCallback cb) {
        thread_init_cb_ = std::move(cb);
    }

    // 開始監聽。可重複呼叫，只有第一次有效（thread-safe）
    void start();

    void set_connection_callback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_cb_ = std::move(cb); }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const InetAddress& listen_address() const noexcept { return listen_addr_; }
    [[nodiscard]] EventLoop* loop() const noexcept { return loop_; }

private:
    void handle_accept();  // Acceptor 的工作：從監聽 socket 取出新連線
    void new_connection(int sockfd, const InetAddress& peer_addr);
    void remove_connection(const TcpConnectionPtr& conn);
    void remove_connection_in_loop(const TcpConnectionPtr& conn);

    EventLoop* loop_;
    const std::string name_;
    Socket accept_socket_;
    InetAddress listen_addr_;  // bind 之後的實際位址（port 0 時由核心決定）
    Channel accept_channel_;
    int idle_fd_;  // 預留的 fd，用來處理 EMFILE（見 handle_accept）

    std::unique_ptr<EventLoopThreadPool> thread_pool_;
    EventLoopThreadPool::ThreadInitCallback thread_init_cb_;

    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    WriteCompleteCallback write_complete_cb_;

    std::atomic<bool> started_{false};
    int next_conn_id_{1};
    std::unordered_map<std::string, TcpConnectionPtr> connections_;  // 只在 base loop 存取
};

}  // namespace tinynet
