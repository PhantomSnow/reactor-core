# Coverage

Measured with GCC 11.4 (`--coverage -O0`, Debug) and gcovr 8.8 on the library sources only
(`src/` and `include/`; tests, examples and benchmarks excluded). Branch numbers exclude
compiler-generated exception and unreachable branches.

## Baseline (tag `baseline`, measured 2026-11-26)

| File | Lines | Line % | Branches | Branch % |
| --- | ---: | ---: | ---: | ---: |
| src/Buffer.cpp | 52 / 53 | 98% | 20 / 25 | 80% |
| src/Timer.cpp | 85 / 89 | 95% | 56 / 66 | 84% |
| src/Channel.cpp | 35 / 37 | 94% | 17 / 29 | 58% |
| src/EpollPoller.cpp | 41 / 50 | 82% | 28 / 47 | 59% |
| src/EventLoop.cpp | 96 / 103 | 93% | 42 / 59 | 71% |
| src/EventLoopThread.cpp | 30 / 30 | 100% | 14 / 16 | 87% |
| src/EventLoopThreadPool.cpp | 25 / 27 | 92% | 17 / 23 | 73% |
| src/InetAddress.cpp | 22 / 27 | 81% | 10 / 10 | 100% |
| src/Poller.cpp | 5 / 5 | 100% | 4 / 5 | 80% |
| src/Socket.cpp | 49 / 66 | 74% | 18 / 31 | 58% |
| src/TcpConnection.cpp | 99 / 145 | 68% | 63 / 154 | 40% |
| src/TcpServer.cpp | 68 / 83 | 81% | 38 / 65 | 58% |
| **Total (incl. headers)** | **671 / 779** | **86%** | **328 / 531** | **61%** |

Observation: line coverage is already high at baseline, so it is a weak target. Branch
coverage shows the real gaps, especially in `TcpConnection` (40%) and the error paths of
`Socket`, `EpollPoller` and `TcpServer`.

## Progress

| After | `Buffer.cpp` lines | `Buffer.cpp` branches | Library branches |
| --- | ---: | ---: | ---: |
| baseline | 52 / 53 (98%) | 20 / 25 (80%) | 328 / 531 (61%) |
| #4 Buffer tests (B-01..B-14) | 53 / 53 (100%) | 21 / 25 (84%) | 329 / 531 (62%) |

The 4 `Buffer.cpp` branches still uncovered are exactly the `assert` failure sides on lines 20, 29,
42 and 51, which are intentionally uncovered (test strategy, section 7). All 21 reachable branches
of `Buffer.cpp` are now covered.

### #5 TimerQueue tests (T-01..T-09)

| `Timer.cpp` | Baseline | After #5 |
| --- | ---: | ---: |
| Lines | 85 / 89 (95%) | 87 / 89 (97%) |
| Branches | 56 / 66 (84%) | 60 / 66 (90%) |
| Reachable branches | 56 / 60 | 60 / 60 (100%) |

The 6 uncovered `Timer.cpp` branches are: `timerfd_create` failure (lines 21-22, out of scope) and
the null-pointer check the compiler generates for each `delete` (lines 40, 71, 118; the pointers are
never null). See test strategy, section 7.

### Run-to-run variation (found while measuring #5)

Repeating the same measurement gives two different totals, because `tcp_server_test` takes
timing-dependent paths through `TcpConnection` (63 or 50 of 154 branches). Seven runs each:

| Suite | `TcpConnection.cpp` branches | Library branches (total of 531) |
| --- | --- | --- |
| baseline | 63 (4 runs) or 50 (3 runs) | 327-329 or 311-312 |
| after #5 | 63 (3 runs) or 50 (4 runs) | 333 or 316 |

`Buffer.cpp` and `Timer.cpp` numbers are identical in every run. Totals should therefore be compared
within the same `TcpConnection` mode: the gain from #4 and #5 is +5 branches in both modes. The
deterministic TcpConnection tests C-01..C-03 are meant to remove this variation.

### Mutation check for #4

To confirm the new tests detect real defects, not just raise coverage, the grow-vs-move condition in
`Buffer::make_space` was changed by hand from `<` to `<=` (an off-by-one):

| Test suite | Result on the mutant |
| --- | --- |
| Baseline `buffer_test.cpp` (6 tests) | 6 passed: mutant survives |
| After #4 (20 tests) | `b06_append_at_move_boundary_compacts` fails: mutant killed |

### Mutation check for #5

| Mutant | Original tests | After #5 |
| --- | --- | --- |
| `reset()` ignores `canceling_timers_` (cancelled repeating timer is rescheduled) | killed by `repeating_timer_can_cancel_itself` | killed by T-06 and T-09 |
| `reset_timerfd` drops the 100 us minimum (a zero delay disarms the timerfd) | survives: 11 passed | killed: `timer_test` hangs and is stopped by the timeout |

## Final

_To be filled in by #11._

## Note on multi-threaded coverage counters

The first CI run failed with `NegativeHitsError` on `TcpConnection.cpp`: gcov's counters are plain
increments, so two I/O threads updating the same counter at once can corrupt it. All measurements
from #6 on use `-fprofile-update=atomic`, which makes the counter updates atomic. Re-measuring with
it gives the same two modes as before (316 or 333-334 library branches), so the earlier numbers stand.

## How to reproduce

```bash
git archive baseline | tar -x -C /tmp/cov-src        # or use the current checkout
cmake -S /tmp/cov-src -B /tmp/cov-build -DCMAKE_BUILD_TYPE=Debug \
      -DTINYNET_BUILD_EXAMPLES=OFF -DTINYNET_BUILD_BENCHMARKS=OFF \
      -DCMAKE_CXX_FLAGS="--coverage -O0 -fprofile-update=atomic" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build /tmp/cov-build -j
(cd /tmp/cov-build && ctest)
pip install gcovr
gcovr -r /tmp/cov-src /tmp/cov-build --filter '/tmp/cov-src/src/' --filter '/tmp/cov-src/include/' \
      --exclude-throw-branches --exclude-unreachable-branches --txt-metric branch
```

## Defects found

_To be filled in by #8._
