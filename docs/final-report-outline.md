# Final Report Outline (draft)

Working skeleton for `8_FinalReport.pdf` (A4, single-spaced, 10–15 pages excluding code and appendices).
Follows the course's *Final Project Presentation and Report Guidelines*. Fill in as the work happens.

## A. Cover Page

- Title: Test Hardening of reactor-core
- Wei-Yen Hsieh (謝瑋諺, 315552055), Group 8 (solo)
- Category: <decide: Category 3, or Category 3 + 4>
- Repository: https://github.com/PhantomSnow/reactor-core (baseline tag: `baseline`)

## B. Executive Summary

One paragraph: what was done, why it matters, key results (coverage before → after, bugs found, harnesses in CI).

## C. Introduction & Problem Statement

- Context: reactor-core (tinynet), one-loop-per-thread Reactor library in C++17
- Motivation: untrusted input + multi-threading; bug classes example-based tests miss
- Description of the software (Cat 3/4): architecture diagram from README

## D. Test Strategy Document (core grading artifact)

### D1. Testing scope
- In scope:
- Out of scope (and why):

### D2. Risk assessment
| Module | Failure mode | Impact | Likelihood | Priority |
| --- | --- | --- | --- | --- |
| Buffer | out-of-bounds read/write | | | |
| TcpConnection | use-after-free after close | | | |
| EventLoop | lost wakeup / data race | | | |
| TimerQueue | wrong expiry order, cancel during callback | | | |

### D3. Equivalence partitioning
| Module / input | Partitions |
| --- | --- |
| | |

### D4. Boundary value analysis
| Module / input | Boundaries probed |
| --- | --- |
| Buffer | empty, 1 byte, exactly full, read crossing 64 KB extra buffer |
| TimerQueue | zero delay, identical expiry times |

### D5. Fuzzing / automation
- Harnesses, sanitizers, CI configuration

## E. Core Technical Work

### Category 3 (required if claiming Cat 3)
- **TDD journey:** graph of tests written vs. code written over time (from git history)
- **Safe refactoring example:** one concrete refactor enabled by the test suite (link the PR)

### Category 4 (required only if claiming Cat 4)
- **Architecture:** diagram of the fuzzing harnesses (how bytes become operations/events)
- **Validation case study:** results of running the harnesses on a real external target

### Results
- Coverage before/after (line, branch) per module
- Bugs found: table (ID, module, sanitizer/harness, root cause, fix PR, regression test)
- Fuzz throughput (exec/s) and corpus coverage growth

## F. AI Process Statement (required)

From `docs/ai-log.md`: tools, 1–2 good prompts, how outputs were verified.

## G. Conclusion & Reflections

- Hardest part:
- With 2 more weeks:

## Appendix

- How to reproduce results (commands)
