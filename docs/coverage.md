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

### Mutation check for #4

To confirm the new tests detect real defects, not just raise coverage, the grow-vs-move condition in
`Buffer::make_space` was changed by hand from `<` to `<=` (an off-by-one):

| Test suite | Result on the mutant |
| --- | --- |
| Baseline `buffer_test.cpp` (6 tests) | 6 passed: mutant survives |
| After #4 (20 tests) | `b06_append_at_move_boundary_compacts` fails: mutant killed |

## Final

_To be filled in by #11._

## How to reproduce

```bash
git archive baseline | tar -x -C /tmp/cov-src        # or use the current checkout
cmake -S /tmp/cov-src -B /tmp/cov-build -DCMAKE_BUILD_TYPE=Debug \
      -DTINYNET_BUILD_EXAMPLES=OFF -DTINYNET_BUILD_BENCHMARKS=OFF \
      -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build /tmp/cov-build -j
(cd /tmp/cov-build && ctest)
pip install gcovr
gcovr -r /tmp/cov-src /tmp/cov-build --filter '/tmp/cov-src/src/' --filter '/tmp/cov-src/include/' \
      --exclude-throw-branches --exclude-unreachable-branches --txt-metric branch
```

## Defects found

_To be filled in by #8._
