#include "tinynet/Buffer.h"

#include <unistd.h>

#include <cerrno>
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

// ---------------------------------------------------------------------------
// Test Strategy cases B-01 .. B-14 (docs/test-strategy.md, sections 4-6).
// Notation: R = readable, W = writable, P = prependable bytes.
// ---------------------------------------------------------------------------

namespace {

void write_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

std::string pattern(std::size_t len) {
    std::string s(len, '\0');
    for (std::size_t i = 0; i < len; ++i) s[i] = static_cast<char>('a' + i % 26);
    return s;
}

}  // namespace

// B-01  EP retrieve(len = 0), BVA 0
TEST(b01_retrieve_zero_is_noop) {
    Buffer buf;
    buf.append("abc");
    buf.retrieve(0);
    CHECK_EQ(buf.readable_bytes(), 3u);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
    CHECK_EQ(std::string(buf.view()), std::string("abc"));
}

// B-02  EP 0 < len < R, BVA 1 and R - 1
TEST(b02_partial_retrieve_moves_reader_only) {
    Buffer buf;
    buf.append("abcdef");
    const std::size_t writable_before = buf.writable_bytes();
    buf.retrieve(1);
    CHECK_EQ(std::string(buf.view()), std::string("bcdef"));
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend + 1);
    buf.retrieve(buf.readable_bytes() - 1);  // R - 1
    CHECK_EQ(std::string(buf.view()), std::string("f"));
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend + 5);
    CHECK_EQ(buf.writable_bytes(), writable_before);
}

// B-03  EP len = R, BVA R: both indices reset
TEST(b03_retrieve_everything_resets_indices) {
    Buffer buf;
    buf.append(std::string(100, 'x'));
    buf.retrieve(50);
    buf.retrieve(buf.readable_bytes());  // len == R
    CHECK_EQ(buf.readable_bytes(), 0u);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
    CHECK_EQ(buf.writable_bytes(), Buffer::kInitialSize);
}

// B-04  BVA append exactly W bytes: fits, no reallocation
TEST(b04_append_exactly_writable_keeps_capacity) {
    Buffer buf;
    const std::size_t capacity = buf.capacity();
    const std::string data = pattern(buf.writable_bytes());
    buf.append(data);
    CHECK_EQ(buf.capacity(), capacity);
    CHECK_EQ(buf.writable_bytes(), 0u);
    CHECK_EQ(buf.retrieve_all_as_string(), data);
}

// B-05  BVA append W + 1 bytes: must grow, content intact
TEST(b05_append_one_past_writable_grows) {
    Buffer buf;
    const std::size_t capacity = buf.capacity();
    const std::string data = pattern(buf.writable_bytes() + 1);
    buf.append(data);
    CHECK(buf.capacity() > capacity);
    CHECK_EQ(buf.retrieve_all_as_string(), data);
}

// Shared setup for B-06/B-07: R = 300, W = 224, P = 508 on a 1024-byte buffer.
static void make_front_gap(Buffer& buf, std::string& kept) {
    const std::string first = pattern(800);
    buf.append(first);
    buf.retrieve(500);
    kept = first.substr(500);
}

// B-06  BVA append W + P - 8 bytes: compacts (moves data), capacity unchanged
TEST(b06_append_at_move_boundary_compacts) {
    Buffer buf;
    std::string kept;
    make_front_gap(buf, kept);
    const std::size_t capacity = buf.capacity();
    const std::size_t len = buf.writable_bytes() + buf.prependable_bytes() - Buffer::kCheapPrepend;
    const std::string extra(len, 'B');
    buf.append(extra);
    CHECK_EQ(buf.capacity(), capacity);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
    CHECK_EQ(buf.writable_bytes(), 0u);
    CHECK_EQ(buf.retrieve_all_as_string(), kept + extra);
}

// B-07  BVA append W + P - 7 bytes: one byte too many to compact, must grow
TEST(b07_append_past_move_boundary_grows) {
    Buffer buf;
    std::string kept;
    make_front_gap(buf, kept);
    const std::size_t capacity = buf.capacity();
    const std::size_t len = buf.writable_bytes() + buf.prependable_bytes() - Buffer::kCheapPrepend + 1;
    const std::string extra(len, 'C');
    buf.append(extra);
    CHECK(buf.capacity() > capacity);
    CHECK_EQ(buf.retrieve_all_as_string(), kept + extra);
}

// B-08  BVA prepend 0 and P (= kCheapPrepend) bytes on a fresh buffer
TEST(b08_prepend_full_cheap_prepend_area) {
    Buffer buf;
    buf.append("body");
    buf.prepend("", 0);
    CHECK_EQ(buf.prependable_bytes(), Buffer::kCheapPrepend);
    const std::string header = "HEADER!!";  // exactly 8 bytes
    CHECK_EQ(header.size(), Buffer::kCheapPrepend);
    buf.prepend(header.data(), header.size());
    CHECK_EQ(buf.prependable_bytes(), 0u);
    CHECK_EQ(buf.retrieve_all_as_string(), header + "body");
}

// B-09  EP read_fd n < 0: returns -1 and saves errno
TEST(b09_read_fd_error_saves_errno) {
    Buffer buf;
    int saved_errno = 0;
    const ssize_t n = buf.read_fd(-1, &saved_errno);
    CHECK_EQ(n, static_cast<ssize_t>(-1));
    CHECK_EQ(saved_errno, EBADF);
    CHECK_EQ(buf.readable_bytes(), 0u);
}

// B-10  EP read_fd n = 0: peer closed (EOF)
TEST(b10_read_fd_eof_returns_zero) {
    int fds[2];
    CHECK(::pipe(fds) == 0);
    ::close(fds[1]);
    Buffer buf;
    int saved_errno = 0;
    CHECK_EQ(buf.read_fd(fds[0], &saved_errno), static_cast<ssize_t>(0));
    CHECK_EQ(buf.readable_bytes(), 0u);
    ::close(fds[0]);
}

// B-11  BVA read_fd with exactly W bytes pending: fits, no growth
TEST(b11_read_fd_exactly_writable) {
    int fds[2];
    CHECK(::pipe(fds) == 0);
    Buffer buf;
    const std::size_t capacity = buf.capacity();
    const std::string data = pattern(buf.writable_bytes());
    write_all(fds[1], data);
    int saved_errno = 0;
    CHECK_EQ(buf.read_fd(fds[0], &saved_errno), static_cast<ssize_t>(data.size()));
    CHECK_EQ(buf.capacity(), capacity);
    CHECK_EQ(buf.retrieve_all_as_string(), data);
    ::close(fds[0]);
    ::close(fds[1]);
}

// B-12  BVA read_fd with W + 1 bytes pending: one byte spills into the extra buffer
TEST(b12_read_fd_one_past_writable_spills) {
    int fds[2];
    CHECK(::pipe(fds) == 0);
    Buffer buf;
    const std::string data = pattern(buf.writable_bytes() + 1);
    write_all(fds[1], data);
    int saved_errno = 0;
    CHECK_EQ(buf.read_fd(fds[0], &saved_errno), static_cast<ssize_t>(data.size()));
    CHECK_EQ(buf.retrieve_all_as_string(), data);
    ::close(fds[0]);
    ::close(fds[1]);
}

// B-13  EP one vs. two iovecs, BVA W = 65535 / 65536
TEST(b13_read_fd_large_buffer_boundary) {
    for (std::size_t size : {std::size_t{65535}, std::size_t{65536}}) {
        int fds[2];
        CHECK(::pipe(fds) == 0);
        Buffer buf(size);
        CHECK_EQ(buf.writable_bytes(), size);
        const std::string data = pattern(1000);
        write_all(fds[1], data);
        int saved_errno = 0;
        CHECK_EQ(buf.read_fd(fds[0], &saved_errno), static_cast<ssize_t>(data.size()));
        CHECK_EQ(buf.retrieve_all_as_string(), data);
        ::close(fds[0]);
        ::close(fds[1]);
    }
}

// B-14  EP/BVA find_crlf: lone '\r' at the end, CRLF at index 0, CRLF as the last two bytes
TEST(b14_find_crlf_boundaries) {
    Buffer lone_cr;
    lone_cr.append("abc\r");
    CHECK(lone_cr.find_crlf() == nullptr);

    Buffer at_start;
    at_start.append("\r\nrest");
    CHECK(at_start.find_crlf() == at_start.peek());

    Buffer at_end;
    at_end.append("end\r\n");
    CHECK(at_end.find_crlf() == at_end.peek() + 3);

    Buffer empty;
    CHECK(empty.find_crlf() == nullptr);
}

TEST_MAIN()
