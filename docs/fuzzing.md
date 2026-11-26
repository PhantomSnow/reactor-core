# Fuzzing

## Buffer fuzzer (#7)

`fuzz/buffer_fuzzer.cpp` is a [libFuzzer](https://llvm.org/docs/LibFuzzer.html) harness for
`tinynet::Buffer`, built with AddressSanitizer and UndefinedBehaviorSanitizer.

### How it works

1. libFuzzer generates byte strings and keeps those that reach new code (coverage-guided).
2. The harness decodes each byte string into a sequence of Buffer operations: `append`,
   `retrieve`, `retrieve_as_string`, `retrieve_all`, `prepend`, writing in place through
   `ensure_writable` / `begin_write` / `has_written`, `find_crlf` and `retrieve_until`.
3. Each operation is repeated on a `std::string` **reference model**. After every operation the
   harness checks:
   - the Buffer holds exactly the model's bytes, in order
   - `prependable + readable + writable == capacity`
   - `append` follows the space rules from the test strategy (section 5): fits without moving,
     compacts without growing when `len + 8 <= W + P`, grows otherwise
   - `find_crlf` returns the same position as `std::string::find("\r\n")`
4. A failed check calls `__builtin_trap()`. libFuzzer reports it as a crash and writes the input
   to a `crash-<hash>` file, which reproduces the failure exactly.

Sanitizers catch crashes and memory errors. The reference model and the invariants catch the
other kind of defect: wrong results that do not crash.

The preconditions that `Buffer` enforces with `assert` (`retrieve(n)` with `n <= readable`,
`prepend(n)` with `n <= prependable`) are respected by the harness, so an assertion firing would be
a real defect. This is how the `assert` branches listed as intentionally uncovered in the test
strategy are still exercised: the fuzzer runs millions of operation sequences right up to those
limits.

### Results (2026-11-26, Clang 18.1, `-O1`, ASan + UBSan)

| Run | Executions | Speed | Coverage (`cov` / `ft`) | Corpus | Result |
| --- | ---: | ---: | --- | ---: | --- |
| First version of the harness | 1 228 | — | — | — | Crash: **harness bug**, see below |
| Fixed harness, 5 minutes | 6 514 740 | 21 643 exec/s | 229 / 1 571 | 353 inputs | No defects found |

### A defect in the harness, found by the fuzzer

The first version checked the "compacts" rule as `len <= W + P - 8`. After a `prepend`, the
prependable space P can be smaller than 8. Because the values are unsigned (`size_t`),
`W + P - 8` then wraps around to a huge number, and the harness wrongly expected compaction.

| Before `append(11)` | After |
| --- | --- |
| W = 0, P = 2, capacity = 8 | capacity = 19 (grew, which is correct) |

`Buffer` itself was right: it writes the condition as `W + P < len + 8`, with no subtraction, so it
cannot underflow. The harness was changed to the same form (`len + 8 <= W + P`), and the input that
exposed the problem was kept in the corpus as `regress-prepend-underflow`.

### Mutation check

The same off-by-one used for #4 (changing `<` to `<=` in `Buffer::make_space`) was given to the
fuzzer, without telling it anything about that boundary:

| Suite | Result on the mutant |
| --- | --- |
| Original unit tests (6) | Not detected |
| Unit tests after #4 (B-06) | Detected |
| Buffer fuzzer | Detected after about 6 700 executions (under 1 second) |

The unit test found it because the boundary was worked out by hand; the fuzzer found it without
anyone having to think of that boundary.

## Corpus

`fuzz/corpus/buffer/` holds small starting inputs and every reproducer worth keeping. CI copies it
to a scratch directory before fuzzing, so new inputs found in CI are not committed automatically.

| File | Purpose |
| --- | --- |
| `empty_ops`, `append_small`, `http_like`, `mixed` | Hand-made seeds |
| `regress-prepend-underflow` | Reproducer of the harness bug above |

## In CI

The `fuzz (buffer, 60s)` job runs the fuzzer for 60 seconds on every pull request and push to
`main`. Its statistics are shown on the run's summary page; if it crashes, the reproducer is
uploaded as the `fuzz-crashes` artifact.

## How to run locally

Requires Clang with the libFuzzer runtime (on Ubuntu: `clang` and `libclang-rt-18-dev`).

```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-O1 -g" \
    -DTINYNET_BUILD_FUZZERS=ON -DTINYNET_SANITIZER="address,undefined" \
    -DTINYNET_BUILD_TESTS=OFF -DTINYNET_BUILD_EXAMPLES=OFF -DTINYNET_BUILD_BENCHMARKS=OFF
cmake --build build-fuzz --target buffer_fuzzer
mkdir -p /tmp/corpus && cp fuzz/corpus/buffer/* /tmp/corpus/
./build-fuzz/fuzz/buffer_fuzzer -max_total_time=300 /tmp/corpus

# Reproduce a crash:
./build-fuzz/fuzz/buffer_fuzzer crash-<hash>
```
