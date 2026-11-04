#include "tinynet/EventLoop.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tinynet/EventLoopThread.h"
#include "tinynet/EventLoopThreadPool.h"

using namespace tinynet;
using namespace std::chrono_literals;

TEST(current_loop_is_thread_local) {
    CHECK(EventLoop::current() == nullptr);
    {
        EventLoop loop;
        CHECK(EventLoop::current() == &loop);
        CHECK(loop.is_in_loop_thread());
        std::thread t([&] { CHECK(EventLoop::current() == nullptr); CHECK(!loop.is_in_loop_thread()); });
        t.join();
    }
    CHECK(EventLoop::current() == nullptr);
}

TEST(run_in_loop_from_other_thread_executes_in_loop_thread) {
    EventLoop loop;
    const auto loop_tid = std::this_thread::get_id();
    std::atomic<bool> ran_in_loop_thread{false};

    std::thread t([&] {
        loop.run_in_loop([&] {
            ran_in_loop_thread = std::this_thread::get_id() == loop_tid;
            loop.quit();
        });
    });
    loop.loop();  // run_in_loop 透過 eventfd 喚醒 epoll_wait
    t.join();
    CHECK(ran_in_loop_thread.load());
}

TEST(queue_in_loop_from_pending_functor_still_runs) {
    EventLoop loop;
    int steps = 0;
    loop.queue_in_loop([&] {
        ++steps;
        // 在 do_pending_functors 期間又排入工作：必須喚醒，否則會等到 epoll_wait 逾時（10 秒）
        loop.queue_in_loop([&] {
            ++steps;
            loop.quit();
        });
    });
    const auto start = std::chrono::steady_clock::now();
    std::thread waker([&] { loop.wakeup(); });
    loop.loop();
    waker.join();
    CHECK_EQ(steps, 2);
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST(timers_fire_in_deadline_order) {
    EventLoop loop;
    std::vector<int> order;
    loop.run_after(30ms, [&] { order.push_back(3); });
    loop.run_after(10ms, [&] { order.push_back(1); });
    loop.run_after(20ms, [&] { order.push_back(2); });
    loop.run_after(50ms, [&] { loop.quit(); });

    const auto start = std::chrono::steady_clock::now();
    loop.loop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_EQ(order, (std::vector<int>{1, 2, 3}));
    CHECK(elapsed >= 50ms);
}

TEST(repeating_timer_and_cancel) {
    EventLoop loop;
    int ticks = 0;
    int ticks_at_cancel = -1;
    TimerId every = loop.run_every(10ms, [&] { ++ticks; });
    loop.run_after(55ms, [&] {
        ticks_at_cancel = ticks;
        loop.cancel(every);
    });
    loop.run_after(120ms, [&] { loop.quit(); });
    loop.loop();
    // 不檢查精確次數（機器忙碌時排程會延遲），只檢查真正重要的性質：
    // 有重複觸發，且取消之後完全不再觸發
    CHECK(ticks_at_cancel >= 2);
    CHECK_EQ(ticks, ticks_at_cancel);
}

TEST(repeating_timer_can_cancel_itself) {
    EventLoop loop;
    int ticks = 0;
    TimerId id;
    id = loop.run_every(5ms, [&] {
        if (++ticks == 3) loop.cancel(id);
    });
    loop.run_after(80ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(ticks, 3);
}

TEST(cancelled_one_shot_timer_never_fires) {
    EventLoop loop;
    bool fired = false;
    TimerId id = loop.run_after(20ms, [&] { fired = true; });
    loop.cancel(id);
    loop.run_after(50ms, [&] { loop.quit(); });
    loop.loop();
    CHECK(!fired);
}

TEST(event_loop_thread_runs_work_off_main_thread) {
    EventLoopThread elt({}, "worker");
    EventLoop* loop = elt.start_loop();
    CHECK(loop != nullptr);
    CHECK(!loop->is_in_loop_thread());

    std::atomic<int> done{0};
    std::atomic<bool> other_thread{false};
    loop->run_in_loop([&] {
        other_thread = loop->is_in_loop_thread();
        ++done;
    });
    loop->run_after(5ms, [&] { ++done; });
    while (done.load() < 2) std::this_thread::sleep_for(1ms);
    CHECK(other_thread.load());
    // 析構時 quit + join
}

TEST(quit_before_loop_starts_is_not_lost) {
    // 回歸測試：start_loop() 回傳後立刻析構。析構呼叫 quit() 時，
    // 工作執行緒可能還沒進入 loop()；quit 不能被吃掉，否則 join() 會永遠卡住。
    for (int i = 0; i < 200; ++i) {
        EventLoopThread elt;
        elt.start_loop();
    }
    CHECK(true);  // 能跑到這裡就代表沒有卡死（ctest 有 60 秒 timeout）
}

TEST(thread_pool_round_robin) {
    EventLoop base;
    EventLoopThreadPool pool(&base, "pool");
    pool.set_thread_num(3);
    std::atomic<int> inits{0};
    pool.start([&](EventLoop*) { ++inits; });
    CHECK_EQ(inits.load(), 3);

    EventLoop* a = pool.get_next_loop();
    EventLoop* b = pool.get_next_loop();
    EventLoop* c = pool.get_next_loop();
    EventLoop* d = pool.get_next_loop();
    CHECK(a != b && b != c && a != c);
    CHECK(a != &base);
    CHECK_EQ(d, a);
    CHECK_EQ(pool.get_all_loops().size(), 3u);
}

TEST(thread_pool_with_zero_threads_uses_base_loop) {
    EventLoop base;
    EventLoopThreadPool pool(&base, "pool0");
    pool.start();
    CHECK_EQ(pool.get_next_loop(), &base);
}

TEST_MAIN()
