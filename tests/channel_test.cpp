#include "tinynet/Channel.h"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <memory>

#include "test_util.h"
#include "tinynet/EventLoop.h"

using namespace tinynet;
using namespace std::chrono_literals;

namespace {

struct Pipe {
    int fds[2]{-1, -1};
    Pipe() {
        if (::pipe(fds) != 0) std::abort();
    }
    ~Pipe() {
        ::close(fds[0]);
        ::close(fds[1]);
    }
    int read_end() const { return fds[0]; }
    int write_end() const { return fds[1]; }
    void put() const { CHECK(::write(fds[1], "x", 1) == 1); }
    void drain() const {
        char buf[16];
        CHECK(::read(fds[0], buf, sizeof(buf)) > 0);
    }
};

}  // namespace

TEST(read_callback_fires_when_readable) {
    EventLoop loop;
    Pipe p;
    Channel channel(&loop, p.read_end());
    int reads = 0;
    channel.set_read_callback([&] {
        p.drain();
        ++reads;
        loop.quit();
    });
    channel.enable_reading();
    CHECK(loop.has_channel(&channel));

    p.put();
    loop.loop();
    CHECK_EQ(reads, 1);

    channel.disable_all();
    channel.remove();
    CHECK(!loop.has_channel(&channel));
}

TEST(disabled_channel_gets_no_events) {
    EventLoop loop;
    Pipe p;
    Channel channel(&loop, p.read_end());
    int reads = 0;
    channel.set_read_callback([&] { ++reads; });
    channel.enable_reading();
    channel.disable_reading();  // 暫時從 epoll 拿掉，但 Poller 仍記得它
    CHECK(loop.has_channel(&channel));

    p.put();
    loop.run_after(50ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(reads, 0);

    // 再次啟用時，資料仍在 pipe 裡，LT 模式會立刻通知
    channel.set_read_callback([&] {
        ++reads;
        p.drain();
        loop.quit();
    });
    channel.enable_reading();
    loop.loop();
    CHECK_EQ(reads, 1);

    channel.disable_all();
    channel.remove();
}

TEST(tie_skips_callbacks_after_owner_dies) {
    EventLoop loop;
    Pipe p;
    Channel channel(&loop, p.read_end());
    int reads = 0;
    channel.set_read_callback([&] { ++reads; });

    auto owner = std::make_shared<int>(42);
    channel.tie(owner);
    channel.enable_reading();
    owner.reset();  // 擁有者已被析構

    p.put();
    loop.run_after(50ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(reads, 0);  // weak_ptr lock 失敗 → 不呼叫回呼，避免使用已釋放的物件

    channel.disable_all();
    channel.remove();
}

TEST_MAIN()
