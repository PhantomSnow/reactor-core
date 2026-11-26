# AI Usage Log

Source material for the report's **AI Process Statement** (required for all teams).
Add one entry each time AI meaningfully shapes code, tests, seeds or docs.

## Tools used

| Tool | Used for |
| --- | --- |
| Claude | Proposal, planning, test strategy, test code, coverage measurement |
| | |

## Log

<!-- Copy this template for each entry, newest at the bottom. -->

### YYYY-MM-DD — <short title>

- **Tool:**
- **Task:** (e.g. generate boundary-value tests for Buffer::retrieve)
- **Prompt:** (paste the exact prompt, or the key part of it)
- **Output used:** (file / PR / commit)
- **Verification:** How did you check it was correct? (ran under ASan, failed at first, compared with spec, reviewed by hand ...)
- **What you had to fix:**
- **Good prompt candidate for the report?** yes / no

### 2026-11-26 — Buffer unit tests B-01..B-14 (#4)

- **Tool:** Claude
- **Task:** Write the 14 Buffer test cases defined in the test strategy
- **Prompt:** "go #4 的 Buffer 測試" (after the test strategy listing B-01..B-14 had been reviewed)
- **Output used:** `tests/buffer_test.cpp` (14 new tests)
- **Verification:** all 20 tests pass under ASan and UBSan; coverage re-measured (all reachable `Buffer.cpp` branches covered); mutation check: changing `<` to `<=` in `make_space` is caught by B-06 but not by the 6 original tests
- **What you had to fix:** nothing in the tests; gcovr run from inside the repo left a stray `.gcov` file, so `*.gcov`/`*.gcda`/`*.gcno` were added to `.gitignore`
- **Good prompt candidate for the report?** no

### 2026-11-26 — TimerQueue tests T-01..T-09 (#5)

- **Tool:** Claude
- **Task:** Write the 9 TimerQueue test cases from the test strategy
- **Prompt:** continued from #4 in the same session
- **Output used:** `tests/timer_test.cpp`, `tests/CMakeLists.txt`
- **Verification:** 9/9 pass, 3 runs each under ASan and UBSan; two mutants of `Timer.cpp` checked (both killed, one of them survives the original tests); coverage measured 7 times because results varied
- **What you had to fix:** the EventLoop API cannot create timers with exactly the same expiry, so T-03 and T-06 drive a `TimerQueue` directly. Repeated measurement showed the library total varies between runs (TcpConnection timing), so single-run totals were replaced by per-mode comparisons
- **Good prompt candidate for the report?** no

## LLM seed experiment (optional AI theme)

| Harness | Seed set | Corpus size | Coverage @10 min | Coverage @60 min |
| --- | --- | --- | --- | --- |
| http_parser | empty | | | |
| http_parser | random | | | |
| http_parser | LLM | | | |
| buffer_ops | empty | | | |
| buffer_ops | random | | | |
| buffer_ops | LLM | | | |
