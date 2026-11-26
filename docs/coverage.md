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
