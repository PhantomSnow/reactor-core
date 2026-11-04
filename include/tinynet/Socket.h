#pragma once

namespace tinynet {

class InetAddress;

// RAII 包裝一個 socket fd：誰擁有 Socket 物件，誰就負責 close。
class Socket {
public:
    // 1. 預設建構：sockfd_ 設為 -1（不擁有任何 fd）
    Socket() noexcept = default;

    // 2. 顯式建構：接管一個 raw fd (例如 accept 出來的 conn_fd)
    explicit Socket(int fd) noexcept : sockfd_(fd) {}

    // 3. 解構子：如果 sockfd_ >= 0，自動 ::close(sockfd_)
    ~Socket();

    // 4. 嚴禁複製 (避免同一個 fd 被 close 兩次)
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // 5. 允許移動 (轉移擁有權)
    Socket(Socket&& rhs) noexcept;
    Socket& operator=(Socket&& rhs) noexcept;

    // 建立 IPv4 TCP socket，一次帶上 SOCK_NONBLOCK | SOCK_CLOEXEC，
    // 省掉一次 fcntl，也避免 fork/exec 時 fd 洩漏到子行程。
    static Socket create_nonblocking_tcp();

    // 6. 取得 fd 與狀態
    [[nodiscard]] int fd() const noexcept { return sockfd_; }
    [[nodiscard]] bool is_valid() const noexcept { return sockfd_ >= 0; }

    // 交出擁有權但不 close，回傳原本的 fd
    [[nodiscard]] int release() noexcept;

    // bind / listen 失敗是設定錯誤，直接丟 std::system_error
    void bind(const InetAddress& addr);
    void listen();

    // 成功回傳新連線 fd（已是 non-blocking + close-on-exec）並填入 peer；
    // 失敗回傳 -1，errno 保留給呼叫端判斷（EAGAIN、EMFILE ...）
    int accept(InetAddress* peer);

    // 半關閉：送出 FIN，但仍可讀取對端尚未送完的資料
    void shutdown_write();

    // 7. 常用屬性設定
    void set_nonblocking(bool on);
    void set_reuse_addr(bool on);
    void set_reuse_port(bool on);
    void set_tcp_nodelay(bool on);
    void set_keep_alive(bool on);

    // 讀取並清除 socket 上的 pending error (SO_ERROR)
    static int get_socket_error(int fd) noexcept;

private:
    int sockfd_{-1};
};

}  // namespace tinynet
