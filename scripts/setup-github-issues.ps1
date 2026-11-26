# One-off setup: labels, weekly milestones and the term-project issues for
# PhantomSnow/reactor-core. Requires GitHub CLI: winget install GitHub.cli ; gh auth login
# Run from the repo root in PowerShell:  .\scripts\setup-github-issues.ps1
$ErrorActionPreference = "Stop"
$repo = "PhantomSnow/reactor-core"

# ---- Labels ----
$labels = @(
  @("test",     "1f883d", "Unit / integration tests"),
  @("fuzz",     "8250df", "Fuzzing harnesses"),
  @("ci",       "0969da", "CI / automation"),
  @("refactor", "bf8700", "Refactoring protected by tests"),
  @("docs",     "6e7781", "Documentation and README"),
  @("report",   "cf222e", "Final report, slides, demo"),
  @("bug",      "d1242f", "Defect found by tests or fuzzing")
)
foreach ($l in $labels) {
  gh label create $l[0] --repo $repo --color $l[1] --description $l[2] --force | Out-Null
}

# ---- Milestones (due at end of day, Taipei time) ----
$milestones = @(
  @("Week 13", "2026-12-07T15:59:59Z", "Test design and unit tests"),
  @("Week 14", "2026-12-14T15:59:59Z", "Automation: CI, fuzzing, refactoring"),
  @("Week 15", "2026-12-21T15:59:59Z", "Results, report, slides, demo"),
  @("Week 16", "2026-12-22T15:59:59Z", "Final presentation and submission")
)
$existing = gh api "repos/$repo/milestones?state=all" --jq ".[].title"
foreach ($m in $milestones) {
  if ($existing -notcontains $m[0]) {
    gh api "repos/$repo/milestones" -f "title=$($m[0])" -f "due_on=$($m[1])" -f "description=$($m[2])" | Out-Null
  }
}

# ---- Issues ----
# Bodies go through a temp file: Windows PowerShell 5.1 mangles quotes in native-command arguments.
function New-Issue($title, $labels, $milestone, $body) {
  $tmp = New-TemporaryFile
  Set-Content -Path $tmp -Value $body -Encoding UTF8
  gh issue create --repo $repo --title $title --label $labels --milestone $milestone --body-file $tmp
  Remove-Item $tmp
}

New-Issue "Add final report outline and AI usage log" "docs,report" "Week 13" @'
Add `docs/final-report-outline.md` (A-G structure from the course guidelines) and `docs/ai-log.md` (source for the AI Process Statement).

**Done when**
- [ ] Both files merged to `main` via PR
'@

New-Issue "Measure baseline coverage" "test,ci" "Week 13" @'
Build the `baseline` tag with `--coverage` (gcov/lcov) and record line and branch coverage per module. This is the "before" number for the report.

**Done when**
- [ ] Coverage of `baseline` recorded in `docs/coverage.md` (per file: line %, branch %)
- [ ] Exact commands written down so the measurement is reproducible
'@

New-Issue "Write Test Strategy for Buffer and TimerQueue" "docs,report" "Week 13" @'
Core grading artifact. Create `docs/test-strategy.md`.

**Done when**
- [ ] Testing scope: in scope / out of scope with reasons
- [ ] Risk assessment table (module, failure mode, impact, likelihood, priority)
- [ ] Equivalence partitioning table for Buffer and TimerQueue inputs
- [ ] Boundary value table listing every boundary probed
- [ ] Each test case in #Buffer / #TimerQueue issues traces back to a row here
'@

New-Issue "Buffer unit tests (equivalence partitioning + boundary values)" "test" "Week 13" @'
Extend `tests/buffer_test.cpp`.

**Cases (minimum)**
- [ ] Empty buffer: readable = 0, retrieve(0), peek on empty
- [ ] 1 byte append / retrieve
- [ ] Append exactly to writable capacity, then 1 byte more (growth)
- [ ] Retrieve more than readable (clamped / asserted behavior documented)
- [ ] `read_fd` with data smaller than, equal to and larger than writable space (crosses the 64 KB extra buffer)
- [ ] Prepend region: prepend into reserved space, prepend when reserved space is exhausted
- [ ] Interleaved append/retrieve keeps content order

Commit tests separately from any production-code change.
'@

New-Issue "TimerQueue unit tests" "test" "Week 13" @'
Add `tests/timer_test.cpp`.

**Cases (minimum)**
- [ ] Zero-delay timer fires on the next loop iteration
- [ ] Several timers with identical expiry time all fire, in insertion order
- [ ] Cancel before expiry: never fires
- [ ] Cancel from inside its own callback (repeating timer stops)
- [ ] Cancel another timer from inside a callback
- [ ] Repeating timer fires N times within the expected window
- [ ] Timers added from another thread via the loop
'@

New-Issue "GitHub Actions CI: build, tests, sanitizers, coverage" "ci" "Week 14" @'
Add `.github/workflows/ci.yml`.

**Done when**
- [ ] Matrix: gcc and clang, Debug
- [ ] ASan + UBSan job runs all tests
- [ ] Coverage job uploads an lcov HTML report as an artifact and prints the summary
- [ ] Branch protection on `main`: PR required, CI must pass
- [ ] CI badge in README
'@

New-Issue "libFuzzer harness for Buffer" "fuzz,test" "Week 14" @'
Add `fuzz/buffer_fuzzer.cpp`: decode fuzz bytes into a sequence of Buffer operations (append, retrieve, prepend, peek, retrieve_all) and check invariants after each op (readable + writable + prependable == capacity, contents match a std::string reference model).

**Done when**
- [ ] Builds with clang `-fsanitize=fuzzer,address,undefined` behind a CMake option
- [ ] Small seed corpus committed under `fuzz/corpus/buffer`
- [ ] CI runs the fuzzer for 60 seconds on every PR
- [ ] Throughput (exec/s) and result recorded for the report
'@

New-Issue "Fix defects found by tests or fuzzing" "bug" "Week 14" @'
For every failing test or fuzzer crash: open a separate `bug` issue, fix it in its own PR, and add a regression test (fuzzer crash input goes to the corpus).

If nothing is found, record that in the report with the coverage and fuzzing time that support it.

**Done when**
- [ ] All findings have a linked bug issue, fix PR and regression test
- [ ] Summary table (ID, module, found by, root cause, fix PR) in `docs/coverage.md`
'@

New-Issue "Safe refactoring of Buffer protected by tests" "refactor" "Week 14" @'
Required by Category 3 ("give one example of code you refactored safely because you had tests").

**Done when**
- [ ] One focused refactor in Buffer (e.g. extract the space-making logic into a helper, remove duplication)
- [ ] All tests green before and after; PR description shows both CI runs
- [ ] Before/after snippet saved for the report
'@

New-Issue "README: How to run tests, fuzzer and coverage" "docs" "Week 14" @'
Course requirement: README must contain complete installation and execution instructions.

**Done when**
- [ ] Prerequisites (Linux/WSL, CMake, gcc/clang)
- [ ] Build and run tests
- [ ] Run sanitizer builds
- [ ] Run the fuzzer
- [ ] Generate the coverage report
'@

New-Issue "Measure final coverage and compare with baseline" "test,report" "Week 15" @'
**Done when**
- [ ] Final line/branch coverage per module recorded next to the baseline in `docs/coverage.md`
- [ ] Before/after table and one chart ready for report and slides
'@

New-Issue "Tests Written vs. Code Written chart from git history" "report" "Week 15" @'
Required by Category 3 ("TDD Journey").

**Done when**
- [ ] Script in `scripts/` counts test vs. production lines added per day/commit from `git log --numstat`
- [ ] Chart image saved under `docs/img/`
'@

New-Issue "Final report 8_FinalReport.pdf" "report" "Week 15" @'
Follow `docs/final-report-outline.md` (A-G). A4, single-spaced, 10-15 pages.

**Done when**
- [ ] Cover page with repo link and Category 3
- [ ] Test Strategy Document section complete
- [ ] Results: coverage table, bug table, tests-vs-code chart, refactoring example
- [ ] AI Process Statement from `docs/ai-log.md`
- [ ] Exported as `8_FinalReport.pdf`
'@

New-Issue "Slides, 10-minute script and demo" "report" "Week 15" @'
**Done when**
- [ ] 8 slides following the course structure, exported as `8_Slides.pdf`
- [ ] Word-for-word script, timed at about 10 minutes (limit 12 + 3 min Q&A)
- [ ] Scripted demo: test fails -> fix -> passes, then fuzzer run; terminal font zoomed
- [ ] Backup screen recording of the demo on the laptop
'@

New-Issue "Final presentation and submission" "report" "Week 16" @'
**Done when**
- [ ] Present on 12/22
- [ ] Upload `8_FinalReport.pdf`, `8_Slides.pdf` and the repo link
- [ ] Repo is public (or TA/professor invited)
'@

Write-Host "Done. Open https://github.com/$repo/issues"
