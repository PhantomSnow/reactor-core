// TimerQueue tests T-01 .. T-09 (docs/test-strategy.md, sections 4-6).
// Most cases go through the EventLoop timer API. T-03 and T-06 need several timers with the
// *same* expiry time, which the EventLoop API cannot express (run_after/run_every read the clock
// separately for each call), so they drive a TimerQueue on the loop directly.
#include "tinynet/Timer.h"

#include <chrono>
#include <vector>

#include "test_util.h"
#include "tinynet/EventLoop.h"

using namespace tinynet;
using namespace std::chrono_literals;

// T-01  EP expiry in the past / now, BVA -1 ms and 0
TEST(t01_past_and_zero_delay_timers_fire) {
    EventLoop loop;
    bool past_fired = false;
    bool zero_fired = false;
    loop.run_at(Clock::now() - 1ms, [&] { past_fired = true; });
    loop.run_after(0ms, [&] { zero_fired = true; });
    loop.run_after(30ms, [&] { loop.quit(); });
    loop.loop();
    CHECK(past_fired);
    CHECK(zero_fired);
}

// T-02  EP expiry less than 100 us ahead (reset_timerfd clamps the delay)
TEST(t02_sub_100us_timer_fires) {
    EventLoop loop;
    bool fired = false;
    loop.run_after(50us, [&] { fired = true; });
    loop.run_after(30ms, [&] { loop.quit(); });
    loop.loop();
    CHECK(fired);
}

// T-03  EP several timers with the same expiry: all fire, order not guaranteed
TEST(t03_same_expiry_timers_all_fire) {
    EventLoop loop;
    TimerQueue queue(&loop);
    const TimePoint when = Clock::now() + 5ms;
    std::vector<int> fired;
    for (int i = 0; i < 3; ++i) {
        queue.add_timer([&fired, i] { fired.push_back(i); }, when, Duration::zero());
    }
    loop.run_after(40ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(fired.size(), 3u);
    int sum = 0;
    for (int v : fired) sum += v;
    CHECK_EQ(sum, 0 + 1 + 2);  // each timer exactly once
}

// T-04  EP expiry later than the current earliest: must not delay the earlier timer
TEST(t04_later_timer_does_not_delay_earlier) {
    EventLoop loop;
    const auto start = Clock::now();
    Duration early_at{};
    bool late_fired = false;
    loop.run_after(5ms, [&] { early_at = Clock::now() - start; });
    loop.run_after(200ms, [&] { late_fired = true; });  // added second, expires later
    loop.run_after(40ms, [&] { loop.quit(); });
    loop.loop();
    CHECK(early_at > Duration::zero());
    CHECK(early_at < 40ms);
    CHECK(!late_fired);
}

// T-05  EP cancel an already fired one-shot timer: no-op, no crash (checked by ASan)
TEST(t05_cancel_already_fired_timer_is_noop) {
    EventLoop loop;
    int fired = 0;
    TimerId id = loop.run_after(1ms, [&] { ++fired; });
    loop.run_after(20ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(fired, 1);
    loop.cancel(id);  // timer object is already deleted; cancel must not touch it
    CHECK_EQ(fired, 1);
}

// T-06  EP cancel another timer of the same expired batch from inside a callback:
//       the cancelled repeating timer must not be rescheduled
TEST(t06_cancel_same_batch_timer_from_callback) {
    EventLoop loop;
    TimerQueue queue(&loop);
    const TimePoint when = Clock::now() + 5ms;
    int repeating_runs = 0;
    TimerId repeating = queue.add_timer([&] { ++repeating_runs; }, when, 5ms);
    queue.add_timer([&] { queue.cancel(repeating); }, when, Duration::zero());
    loop.run_after(60ms, [&] { loop.quit(); });
    loop.loop();
    // It ran at most once in the shared batch (order inside a batch is not guaranteed),
    // and was never rescheduled. Without the cancel it would run about 11 times.
    CHECK(repeating_runs <= 1);
}

// T-07  EP cancel a default-constructed TimerId: no-op, other timers unaffected
TEST(t07_cancel_default_timer_id_is_noop) {
    EventLoop loop;
    bool fired = false;
    loop.cancel(TimerId{});
    loop.run_after(1ms, [&] { fired = true; });
    loop.run_after(20ms, [&] { loop.quit(); });
    loop.loop();
    CHECK(fired);
}

// T-08  EP queue still holding timers at destruction: they are freed (ASan leak check)
TEST(t08_destroying_loop_frees_pending_timers) {
    bool fired = false;
    {
        EventLoop loop;
        loop.run_after(1h, [&] { fired = true; });
        loop.run_every(1h, [&] { fired = true; });
    }  // ~EventLoop -> ~TimerQueue deletes both pending timers
    CHECK(!fired);
}

// T-09  BVA repeating timer cancelled after its 1st firing fires exactly once
TEST(t09_repeating_timer_cancelled_after_first_run) {
    EventLoop loop;
    int runs = 0;
    TimerId id;
    id = loop.run_every(5ms, [&] {
        ++runs;
        loop.cancel(id);
    });
    loop.run_after(50ms, [&] { loop.quit(); });
    loop.loop();
    CHECK_EQ(runs, 1);
}

TEST_MAIN()
