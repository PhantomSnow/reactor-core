#include "tinynet/InetAddress.h"

#include <arpa/inet.h>

#include <stdexcept>

namespace tinynet {

InetAddress::InetAddress(uint16_t port, bool loopback_only) {
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    addr_.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
}

InetAddress::InetAddress(const std::string& ip, uint16_t port) {
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) != 1) {
        throw std::invalid_argument("invalid IPv4 address: " + ip);
    }
}

std::string InetAddress::ip() const {
    char buf[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    return buf;
}

uint16_t InetAddress::port() const noexcept { return ntohs(addr_.sin_port); }

std::string InetAddress::to_ip_port() const { return ip() + ":" + std::to_string(port()); }

InetAddress InetAddress::local_of(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return InetAddress(addr);
}

InetAddress InetAddress::peer_of(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    ::getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return InetAddress(addr);
}

}  // namespace tinynet
