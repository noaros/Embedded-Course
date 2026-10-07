# Lab 11: Pipeline

**Host PC** (Linux/macOS, or WSL) + optionally the NUCLEO-H723ZG · **Time:** 2 h · **Builds on:** Module 11, Lab 4

You take the Lab 4 data logger's FIR filter and command parser, put them in a host-testable library, test them to at least 90% line coverage under AddressSanitizer and UBSan, and set up a CI pipeline that builds the firmware, runs the tests, runs static analysis, and posts a size report on every merge request.

## Files

```
starter/
  CMakeLists.txt         host project: Unity (fetched), lib, tests; options LAB11_SANITIZE, LAB11_COVERAGE
  lib/fir.c/h frame.c/h  from Lab 4, unchanged
  lib/cmd_parser.h       the parser's interface (given)
  lib/cmd_parser.c       TODO: implement, test-first
  tests/test_frame.c     complete: an example of the style
  tests/test_fir.c       two tests + TODO list
  tests/test_cmd_parser.c  one test + TODO list
  ci/README.md           what your pipeline must do
tools/size_report.py     flash/RAM table with deltas (used by the pipeline)
```

Build and run (the native compiler, not arm-none-eabi):

```sh
cmake -S modules/11-testing-ci/lab/starter -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

`test_cmd_parser` fails out of the box. That's your first red test.

## What you hand in

1. `lib/cmd_parser.c` and your three test files.
2. Coverage report: **≥ 90% line coverage** of `lib/` (gcovr text output).
3. Your pipeline file, and a link to (or screenshot of) a green run plus a merge request showing the size-delta table.
4. One bug the sanitizers or static analysis found (deliberately plant one if you found none), with the tool output.
5. Optional: the HIL job running on your board.

## Steps

### 1. Test-drive the parser (60 min)

Work through the TODO list in `tests/test_cmd_parser.c` **one behaviour at a time**: write the test, watch it fail, make it pass, refactor. Commit after each green step, so your history shows the cycle.

The parser must handle everything the UART can throw at it: partial lines, CR/LF/CRLF, over-long lines, garbage. Then replace the inline parsing in your Lab 4 `main.c` with `cmd_parser_feed()`, so the firmware uses the tested code.

### 2. Finish the FIR tests (20 min)

Cover the TODO list in `tests/test_fir.c`. The block-size test is the important one: it's the bug you'd only see on target as an occasional glitch every 1024 samples.

### 3. Coverage (10 min)

```sh
cmake -S modules/11-testing-ci/lab/starter -B build-cov -DLAB11_COVERAGE=ON
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root modules/11-testing-ci/lab/starter --filter '.*/lib/' build-cov
```

Look at the lines you haven't covered. Is each one a missing test, or dead code?

### 4. Make a sanitizer earn its keep (10 min)

Temporarily break `cmd_parser_feed()` so it writes one byte past `line[]` on an over-long line. Run the tests. Then try the same bug in the firmware: what happens on the target?

### 5. The pipeline (30 min)

Write `.github/workflows/ci.yml` (or `.gitlab-ci.yml`) as described in `starter/ci/README.md`. The jobs are:

| Job | Must fail when |
| --- | --- |
| host tests | any test fails, or ASan/UBSan reports anything |
| coverage | line coverage of `lib/` < 90% |
| static analysis | cppcheck reports a warning (style findings may be informational) |
| firmware | the cross-build fails, or a merge request grows any image's flash by more than 2 KB |
| HIL (optional) | the board doesn't stream 60 s of data with zero gaps |

For a HIL job, register a self-hosted runner on a machine with the board attached. `tools/lab4_rx.py` already exits non-zero on any gap or CRC error.

## Stretch goals

- **Fuzz the parser** with libFuzzer (`clang -fsanitize=fuzzer,address`) for 10 minutes. Write the harness in 10 lines.
- **Mock a driver with CMock**: put a `uart_if.h` seam under the logger's TX path and test that a full TX queue drops frames and counts them.
- **clang-tidy** with `bugprone-*,cert-*` checks on `lib/`, using `compile_commands.json`.
- **Ratchet**: store the coverage percentage and make CI fail if it drops below the last value on `main`.

## Grading

| Criterion | Points |
| --- | --- |
| Parser complete; commit history shows test-first steps | 4 |
| FIR tests incl. block-size invariance | 2 |
| ≥ 90% line coverage under ASan + UBSan | 2 |
| Pipeline: tests, coverage gate, static analysis, firmware build | 4 |
| Size report with merge-request delta | 2 |
| Sanitizer/static-analysis finding documented | 1 |
| **Total** | **15** (+2 for working HIL) |
