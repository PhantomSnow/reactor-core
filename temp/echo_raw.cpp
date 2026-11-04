#include <iostream>
#include <cstring>
#include <unistd.h>      // read, write, close
#include <sys/socket.h>  // socket, bind, listen, accept
#include <netinet/in.h>  // sockaddr_in, htons, htonl, INADDR_ANY
#include <arpa/inet.h>   // inet_ntop

int main() {
    // 1. 建立 Socket (IPv4, TCP)
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // 2. 設置地址並 Bind
    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8888);               // 監聽 8888 埠
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 監聽所有本機介面 (0.0.0.0)

    if (::bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << std::endl;
        ::close(listen_fd);
        return 1;
    }

    // 3. 進入 Listen 狀態
    if (::listen(listen_fd, SOMAXCONN) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << std::endl;
        ::close(listen_fd);
        return 1;
    }

    std::cout << "[Server] Listening on port 8888... Waiting for connection." << std::endl;

    // 4. 阻塞等待 Client 連線
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_fd = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (conn_fd < 0) {
        std::cerr << "accept() failed: " << std::strerror(errno) << std::endl;
        ::close(listen_fd);
        return 1;
    }

    // 印出連線進來的 Client IP 與 Port
    char ip_str[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    std::cout << "[Server] Accepted client from " << ip_str 
              << ":" << ntohs(client_addr.sin_port) 
              << " with conn_fd = " << conn_fd << std::endl;

    // 5. Echo 迴圈：聽一句、回一句
    char buf[1024];
    while (true) {
        std::memset(buf, 0, sizeof(buf));
        ssize_t bytes_read = ::read(conn_fd, buf, sizeof(buf) - 1);

        if (bytes_read > 0) {
            std::cout << "[Client says]: " << buf;
            // 回傳相同的字串給客戶端 (Echo)
            ::write(conn_fd, buf, bytes_read);
        } else if (bytes_read == 0) {
            // 關鍵邊界：read 回傳 0 代表什麼？（對端主動呼叫了 close 或發送 FIN）
            std::cout << "[Server] Client disconnected gracefully." << std::endl;
            break;
        } else {
            std::cerr << "read() error: " << std::strerror(errno) << std::endl;
            break;
        }
    }

    // 6. 關閉通話連線與監聽
    ::close(conn_fd);
    ::close(listen_fd);
    std::cout << "[Server] Closed all sockets. Exit." << std::endl;
    return 0;
}