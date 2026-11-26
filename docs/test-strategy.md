# Test Strategy

Scope: the term-project testing work on reactor-core (tinynet), measured against the `baseline` tag.

## 1. Objective and metric

Baseline measurement (see [coverage.md](coverage.md)) showed **86% line coverage but only 61% branch
coverage**. Line coverage is therefore a weak signal for this code: the existing tests run most lines
on the happy path, but rarely take the error side of a decision. **Branch coverage is the primary
metric.**

| Target | Baseline | Goal |
| --- | ---: | ---: |
| `Buffer.cpp` branch coverage | 80% | ≥ 95% of reachable branches |
| `Timer.cpp` branch coverage | 84% | ≥ 95% of reachable branches |
| Library branch coverage (total) | 61% | ≥ 70% |
| Sanitizer findings (ASan/UBSan) on all tests and the fuzzer | — | 0 open |

"Reachable" excludes the intentionally uncovered branches listed in section 7.

## 2. Testing scope

**In scope**
- `Buffer`: every public operation, including the space-management and `read_fd` paths
- `TimerQueue` (through the `EventLoop` timer API): add, fire, repeat, cancel in every timer state
- `TcpConnection`: error and shutdown paths, enough to reach the total branch goal
- Memory and undefined-behaviour checking of all of the above (ASan/UBSan, libFuzzer)

**Out of scope, and why**
- Performance and benchmarks: not a correctness property; covered by the existing benchmark README
- Multi-threaded stress testing under TSan: deferred for time; existing tests already run clean under TSan in development
- `EpollPoller`, `Socket` system-call failure paths: need fault injection into the kernel interface, which is outside the time budget
- Examples (echo/HTTP servers): demonstration code, not library code

## 3. Risk assessment

Impact: what a defect would do to a server built on the library. Likelihood: how complex and how
exposed to untrusted input the code is.

| Module | Failure mode | Impact | Likelihood | Priority |
| --- | --- | --- | --- | --- |
| Buffer | Out-of-bounds read/write when growing or compacting | High: memory corruption from network input | Medium: index arithmetic in `make_space`, `read_fd` | **1** |
| Buffer | Data lost or reordered after compaction | High: silent data corruption | Medium | **1** |
| TimerQueue | Use-after-free when a timer is cancelled while its batch is running | High: crash | Medium: several cancel states | **2** |
| TimerQueue | Cancelled repeating timer fires again / never stops | Medium: wrong behaviour, leak | Medium | **2** |
| TcpConnection | Send after peer close, shutdown with unsent data | High: crash or lost data | Medium: 40% branch coverage at baseline | **3** |
| EventLoop | Lost wakeup on cross-thread task | Medium: hang | Low: already covered by existing tests | 4 |
| Socket / EpollPoller | Unhandled system-call failure | Medium | Low in practice | 5 (out of scope) |

## 4. Equivalence partitioning

Notation: R = readable bytes, W = writable bytes, P = prependable bytes, `kCheapPrepend` = 8,
`kInitialSize` = 1024, extra read buffer = 65 536 bytes.

### Buffer

| Input | Valid partitions | Invalid partitions |
| --- | --- | --- |
| `retrieve(len)` | len = 0 · 0 < len < R (partial) · len = R (everything; indices reset) | len > R (assert) |
| `append(len)` | len ≤ W (fits, no change) · W < len ≤ W + (P − 8) (fits after moving data to the front) · len > W + (P − 8) (vector grows) | — |
| `prepend(len)` | len ≤ P | len > P (assert) |
| `read_fd` result n | n < 0 (error, errno saved) · n = 0 (EOF) · 0 < n ≤ W (fits) · n > W (spills into extra buffer, then appended) | — |
| `read_fd` buffer size | W < 65 536 (two iovecs) · W ≥ 65 536 (one iovec) | — |
| `find_crlf` content | empty · no CRLF · CRLF at start · CRLF in middle · `\r` alone at the end | — |

### TimerQueue

| Input | Valid partitions | Invalid partitions |
| --- | --- | --- |
| Expiry time | in the past · now (zero delay) · less than 100 µs ahead (clamped by `reset_timerfd`) · in the future | — |
| Interval | 0 (one-shot) · > 0 (repeating) | — |
| Expiry relative to queue | queue empty · earlier than current earliest · later than current earliest · equal to another timer | — |
| `cancel` target state | pending · already fired (one-shot) · itself, inside its own callback · another timer of the same expired batch, inside a callback · default `TimerId{}` | — |
| Queue at destruction | empty · still holding pending timers (must be freed) | — |

### TcpConnection

| Input | Partitions |
| --- | --- |
| `send` while state is | connected · disconnecting · disconnected |
| `shutdown` with output buffer | empty · non-empty (must flush first) |
| Peer action | sends data · half-closes (EOF) · resets |

## 5. Boundary value analysis

| Function | Boundary | Values tested |
| --- | --- | --- |
| `retrieve(len)` | 0 and R | 0, 1, R − 1, R |
| `append` vs. writable space | W | W, W + 1 |
| `append` move vs. grow (`make_space`) | W + P − 8 | W + P − 8 (move, capacity unchanged), W + P − 7 (grow) |
| `prepend(len)` | P = 8 on a fresh buffer | 0, 8 |
| `read_fd` fits vs. spills | W | n = W, n = W + 1 |
| `read_fd` one vs. two iovecs | W = 65 536 | W = 65 535, W = 65 536 |
| `find_crlf` | position of `\r\n` | index 0, last two bytes, `\r` as last byte only |
| Timer delay | 0 and the 100 µs clamp | −1 ms, 0, 50 µs, 1 ms |
| Timer interval | 0 | 0 (one-shot), 1 ms (repeat) |
| Repeat count before cancel | 1 | cancel after 1st and after 3rd firing |

## 6. Test cases

Existing tests are kept. New tests are added to `tests/buffer_test.cpp` and a new `tests/timer_test.cpp`
(#4, #5). Each case below traces to a partition (EP) or boundary (BVA) above.

| ID | Case | Covers |
| --- | --- | --- |
| B-01 | `retrieve(0)` leaves the buffer unchanged | EP retrieve, BVA 0 |
| B-02 | `retrieve(1)` and `retrieve(R − 1)` advance the reader only | EP partial, BVA 1, R − 1 |
| B-03 | `retrieve(R)` resets reader and writer to `kCheapPrepend` | EP everything, BVA R |
| B-04 | `append` exactly W bytes: capacity unchanged | BVA W |
| B-05 | `append` W + 1 bytes: grows, content intact | BVA W + 1 |
| B-06 | `append` W + P − 8 bytes after a partial retrieve: moves, capacity unchanged, order kept | BVA move |
| B-07 | `append` W + P − 7 bytes: grows instead of moving | BVA grow |
| B-08 | `prepend` 8 bytes on a fresh buffer | BVA P |
| B-09 | `read_fd` on an invalid fd returns −1 and saves `errno` | EP n < 0 |
| B-10 | `read_fd` on a closed peer returns 0 | EP n = 0 |
| B-11 | `read_fd` with exactly W bytes pending | BVA n = W |
| B-12 | `read_fd` with W + 1 bytes pending | BVA n = W + 1 |
| B-13 | `read_fd` with W ≥ 65 536 (large buffer) | EP one iovec, BVA 65 536 |
| B-14 | `find_crlf` on `\r` alone at the end returns null; at index 0 returns `peek()` | EP / BVA CRLF |
| T-01 | Past and zero-delay timers fire on the next loop iteration | EP past/now, BVA −1 ms, 0 |
| T-02 | 50 µs timer fires (clamp path) | EP < 100 µs |
| T-03 | Several timers with the same expiry all fire (order not guaranteed) | EP equal expiry |
| T-04 | Adding a later timer does not delay an earlier one | EP later than earliest |
| T-05 | Cancelling an already fired one-shot timer is a no-op | EP cancel fired |
| T-06 | Cancelling another timer of the same batch from a callback: it is not rescheduled | EP cancel same batch |
| T-07 | Cancelling a default `TimerId{}` is a no-op | EP default id |
| T-08 | Destroying a loop with pending timers frees them (checked by ASan leak detection) | EP destruction |
| T-09 | Repeating timer cancelled after its 1st firing fires exactly once | BVA repeat 1 |
| C-01 | `send` after the connection is closed is ignored | EP disconnected |
| C-02 | `shutdown` with pending output flushes before FIN | EP non-empty output |
| C-03 | Peer half-close triggers the close callback once | EP EOF |

## 7. Intentionally uncovered branches

| Location | Branch | Reason |
| --- | --- | --- |
| `Buffer.cpp` `assert(...)` (lines 20, 29, 42, 51) | assertion false | Precondition violations abort by design; covered by fuzzer invariants instead of unit tests |
| `Timer.cpp` `create_timerfd` (lines 21–22) | `timerfd_create` fails | Requires exhausting file descriptors; system-call failure is out of scope |

## 8. Techniques and tools

| Technique | Tool | Where |
| --- | --- | --- |
| Unit and integration tests | Project's zero-dependency test framework (`tests/test_util.h`) | `tests/` |
| Equivalence partitioning, boundary value analysis | This document | sections 4–6 |
| Memory and UB checking | AddressSanitizer, UndefinedBehaviorSanitizer | CI job, fuzzer |
| Fuzzing with a reference model | libFuzzer: Buffer operations checked against `std::string` | `fuzz/` (#7) |
| Coverage measurement | gcov + gcovr, branch coverage | `docs/coverage.md` |
| Continuous integration | GitHub Actions | `.github/workflows/ci.yml` (#6) |

Note: the proposal listed GoogleTest. The project already has a small zero-dependency test framework
used by all existing tests, so new tests use it too; this keeps the build free of downloads and the
test style consistent.
