#include "tinynet/Buffer.h"

#include <sys/uio.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>

namespace tinynet {

const char* Buffer::find_crlf() const noexcept {
    static constexpr char kCrlf[] = "\r\n";
    const char* end = begin() + writer_;
    const char* pos = std::search(peek(), end, kCrlf, kCrlf + 2);
    return pos == end ? nullptr : pos;
}

void Buffer::retrieve(std::size_t len) {
    assert(len <= readable_bytes());
    if (len < readable_bytes()) {
        reader_ += len;
    } else {
        retrieve_all();
    }
}

std::string Buffer::retrieve_as_string(std::size_t len) {
    assert(len <= readable_bytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

void Buffer::append(const char* data, std::size_t len) {
    ensure_writable(len);
    std::copy(data, data + len, begin_write());
    has_written(len);
}

void Buffer::prepend(const void* data, std::size_t len) {
    assert(len <= prependable_bytes());
    reader_ -= len;
    std::memcpy(begin() + reader_, data, len);
}

void Buffer::ensure_writable(std::size_t len) {
    if (writable_bytes() < len) {
        make_space(len);
    }
    assert(writable_bytes() >= len);
}

void Buffer::make_space(std::size_t len) {
    if (writable_bytes() + prependable_bytes() < len + kCheapPrepend) {
        // 前後的空閒空間加起來還是不夠：只能擴容
        buf_.resize(writer_ + len);
    } else {
        // 空間其實夠，只是被已讀走的部分佔在前面：把可讀資料搬回開頭即可，不必配置新記憶體
        std::size_t readable = readable_bytes();
        std::copy(begin() + reader_, begin() + writer_, begin() + kCheapPrepend);
        reader_ = kCheapPrepend;
        writer_ = reader_ + readable;
    }
}

ssize_t Buffer::read_fd(int fd, int* saved_errno) {
    char extrabuf[65536];
    iovec vec[2];
    const std::size_t writable = writable_bytes();
    vec[0].iov_base = begin_write();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    // buffer 本身已經夠大時，就不用額外緩衝
    const int iovcnt = writable < sizeof(extrabuf) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0) {
        *saved_errno = errno;
    } else if (static_cast<std::size_t>(n) <= writable) {
        writer_ += static_cast<std::size_t>(n);
    } else {
        writer_ = buf_.size();
        append(extrabuf, static_cast<std::size_t>(n) - writable);
    }
    return n;
}

}  // namespace tinynet
