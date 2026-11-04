#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <string>

namespace tinynet {

// sockaddr_in 的值型別包裝（只支援 IPv4）
class InetAddress {
public:
    static constexpr socklen_t kSockaddrLen = sizeof(sockaddr_in);

    // port = 0 代表讓核心挑一個可用的 port（測試很好用）
    explicit InetAddress(uint16_t port = 0, bool loopback_only = false);
    InetAddress(const std::string& ip, uint16_t port);
    explicit InetAddress(const sockaddr_in& addr) noexcept : addr_(addr) {}

    [[nodiscard]] std::string ip() const;
    [[nodiscard]] uint16_t port() const noexcept;
    [[nodiscard]] std::string to_ip_port() const;

    [[nodiscard]] const sockaddr* sockaddr_ptr() const noexcept {
        return reinterpret_cast<const sockaddr*>(&addr_);
    }

    // 從已連線 / 已 bind 的 fd 查出本端與對端位址
    static InetAddress local_of(int fd);
    static InetAddress peer_of(int fd);

private:
    sockaddr_in addr_{};
};

}  // namespace tinynet
