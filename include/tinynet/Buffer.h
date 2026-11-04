#pragma once

#include <sys/types.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tinynet {

// 應用層緩衝區。TCP 是位元組流：一次 read 可能讀到半個訊息、一次 write 可能
// 只寫出一部分，所以每條連線都需要 input / output buffer。
//
// +-------------------+------------------+------------------+
// | prependable bytes |  readable bytes  |  writable bytes  |
// |                   |    (CONTENT)     |                  |
// +-------------------+------------------+------------------+
// 0      <=      reader_index   <=   writer_index    <=   size
//
// 預留 kCheapPrepend 讓上層可以在訊息前面便宜地補一個長度 header。
class Buffer {
public:
    static constexpr std::size_t kCheapPrepend = 8;
    static constexpr std::size_t kInitialSize = 1024;

    explicit Buffer(std::size_t initial_size = kInitialSize)
        : buf_(kCheapPrepend + initial_size), reader_(kCheapPrepend), writer_(kCheapPrepend) {}

    [[nodiscard]] std::size_t readable_bytes() const noexcept { return writer_ - reader_; }
    [[nodiscard]] std::size_t writable_bytes() const noexcept { return buf_.size() - writer_; }
    [[nodiscard]] std::size_t prependable_bytes() const noexcept { return reader_; }

    [[nodiscard]] const char* peek() const noexcept { return begin() + reader_; }
    [[nodiscard]] std::string_view view() const noexcept { return {peek(), readable_bytes()}; }

    // 找第一個 "\r\n"，找不到回傳 nullptr
    [[nodiscard]] const char* find_crlf() const noexcept;

    void retrieve(std::size_t len);
    void retrieve_until(const char* end) { retrieve(static_cast<std::size_t>(end - peek())); }
    void retrieve_all() noexcept { reader_ = writer_ = kCheapPrepend; }
    std::string retrieve_as_string(std::size_t len);
    std::string retrieve_all_as_string() { return retrieve_as_string(readable_bytes()); }

    void append(const char* data, std::size_t len);
    void append(std::string_view data) { append(data.data(), data.size()); }
    void prepend(const void* data, std::size_t len);

    void ensure_writable(std::size_t len);
    char* begin_write() noexcept { return begin() + writer_; }
    void has_written(std::size_t len) noexcept { writer_ += len; }

    // 直接從 fd 讀進 buffer。用 readv 搭配 stack 上 64KB 的額外緩衝：
    // 一次系統呼叫就能讀很多資料，又不必替每條連線預先配置大 buffer。
    ssize_t read_fd(int fd, int* saved_errno);

    [[nodiscard]] std::size_t capacity() const noexcept { return buf_.size(); }

private:
    char* begin() noexcept { return buf_.data(); }
    const char* begin() const noexcept { return buf_.data(); }
    void make_space(std::size_t len);

    std::vector<char> buf_;
    std::size_t reader_;
    std::size_t writer_;
};

}  // namespace tinynet
