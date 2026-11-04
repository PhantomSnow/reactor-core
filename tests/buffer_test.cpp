#include "tinynet/Buffer.h"

#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "test_util.h"

using tinynet::Buffer;

TEST(append_and_retrieve) {
    Buffer buf;
    CHECK_EQ(buf.readable_bytes(), 0u);
    CHECK_EQ(buf.writable_bytes(), Buffer::kInitialSize);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);

    const std::string data(200, 'x');
    buf.append(data);
    CHECK_EQ(buf.readable_bytes(), data.size());
    CHECK_EQ(buf.writable_bytes(), Buffer::kInitialSize - data.size());

    const std::string first = buf.retrieve_as_string(50);
    CHECK_EQ(first, std::string(50, 'x'));
    CHECK_EQ(buf.readable_bytes(), 150u);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend + 50);

    buf.retrieve_all();
    CHECK_EQ(buf.readable_bytes(), 0u);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
}

TEST(grows_when_full) {
    Buffer buf;
    buf.append(std::string(400, 'y'));
    buf.append(std::string(1000, 'z'));
    CHECK_EQ(buf.readable_bytes(), 1400u);
    CHECK(buf.capacity() >= Buffer::kCheapPrepend + 1400);
    CHECK_EQ(buf.retrieve_as_string(400), std::string(400, 'y'));
    CHECK_EQ(buf.retrieve_all_as_string(), std::string(1000, 'z'));
}

TEST(moves_data_instead_of_growing) {
    Buffer buf;
    buf.append(std::string(800, 'a'));
    buf.retrieve(500);  // 前面空出 500 bytes
    const std::size_t capacity_before = buf.capacity();
    buf.append(std::string(600, 'b'));  // 尾端不夠，但前後加起來夠 → 搬移而不擴容
    CHECK_EQ(buf.capacity(), capacity_before);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
    CHECK_EQ(buf.retrieve_as_string(300), std::string(300, 'a'));
    CHECK_EQ(buf.retrieve_all_as_string(), std::string(600, 'b'));
}

TEST(prepend_header) {
    Buffer buf;
    buf.append("payload");
    const uint32_t len = 7;
    buf.prepend(&len, sizeof(len));
    CHECK_EQ(buf.readable_bytes(), sizeof(len) + 7);
    uint32_t got = 0;
    std::memcpy(&got, buf.peek(), sizeof(got));
    CHECK_EQ(got, len);
}

TEST(find_crlf) {
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\nHost: x\r\n");
    const char* crlf = buf.find_crlf();
    CHECK(crlf != nullptr);
    CHECK_EQ(std::string(buf.peek(), crlf), std::string("GET / HTTP/1.1"));
    buf.retrieve_until(crlf + 2);
    CHECK_EQ(std::string(buf.peek(), buf.find_crlf()), std::string("Host: x"));

    Buffer empty;
    empty.append("no newline");
    CHECK(empty.find_crlf() == nullptr);
}

TEST(read_fd_uses_extra_buffer) {
    // 寫入超過 buffer 初始容量的資料，驗證 readv 的第二段（stack 上的 64KB）會被正確搬進來
    int fds[2];
    CHECK(::pipe(fds) == 0);
    const std::string payload(50000, 'p');
    std::thread writer([&] {
        std::size_t off = 0;
        while (off < payload.size()) {
            ssize_t n = ::write(fds[1], payload.data() + off, payload.size() - off);
            if (n <= 0) break;
            off += static_cast<std::size_t>(n);
        }
        ::close(fds[1]);
    });

    Buffer buf;
    int saved_errno = 0;
    std::size_t total = 0;
    while (true) {
        ssize_t n = buf.read_fd(fds[0], &saved_errno);
        if (n <= 0) break;
        total += static_cast<std::size_t>(n);
    }
    writer.join();
    ::close(fds[0]);
    CHECK_EQ(total, payload.size());
    CHECK_EQ(buf.retrieve_all_as_string(), payload);
}

TEST_MAIN()
