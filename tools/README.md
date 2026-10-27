# Static and dynamic analysis

    make check      # cppcheck on src/ (warnings, style, performance, portability, unused code)
    make analyze    # gcc -fanalyzer plus extra warnings (needs only gcc)
    make valgrind   # run memory-focused tests under Valgrind
    make helgrind   # check test threads for data races and synchronization errors

## cppcheck
Install: `sudo apt install cppcheck` (version 2.x recommended). `make check` fails (exit code 1) on any
finding, so a clean run means nothing is left to explain.

## Valgrind
Install: `sudo apt install valgrind`. `make valgrind` builds the debug-enabled test suite and runs
the blood, donor, storage and utility tests with full leak checks. It skips the menu and user tests,
which spend most of their runtime in password hashing. Invalid memory accesses and definite or indirect
leaks cause a nonzero exit code; other leak categories are printed for inspection but do not fail the
target. Run `make test` for the complete suite.

## Helgrind
Install: `sudo apt install valgrind`. `make helgrind` builds the debug-enabled test suite and runs
all tests under Valgrind's Helgrind thread checker. It reports potential data races, incorrect
mutex/condition-variable usage, and lock-order problems. Thread checking is slower than a regular
test run; a nonzero exit indicates Helgrind found errors (or the test/build failed).

## MISRA checks with cppcheck
cppcheck's MISRA addon needs the text of the MISRA rules, which is licensed and not included here.
Put your copy at `tools/misra_rules.txt` (the addon documents the format), then run this separately
from `make check`:

```sh
PYTHONWARNINGS=ignore::DeprecationWarning cppcheck \
  --addon=tools/misra.json --std=c99 --language=c --inline-suppr --error-exitcode=1 \
  --suppress=misra-config:src/storage.c:342 --suppress=misra-config:src/storage.c:377 \
  --suppress=misra-config:src/storage.c:411 --suppress=misra-config:src/storage.c:444 \
  --suppressions-list=tools/cppcheck-suppressions.txt -Iinclude src
```

The warning filter works around a deprecation warning in cppcheck's installed MISRA addon under
newer Python versions. The line-specific `misra-config` suppressions avoid false positives where
the addon cannot resolve POSIX constants (`ENOENT`, `CLOCK_REALTIME`, and `ETIMEDOUT`) from system
headers. `_POSIX_C_SOURCE` is defined for the analysis to match the build configuration.

## Expected MISRA findings (deliberate, see the README)
| Rule | Where | Why it is accepted |
|---|---|---|
| 21.3 (malloc/free) | `donors.c`, `blood.c`, `users.c`, `storage.c` | The linked lists need dynamic memory. |
| 21.6 (stdio) | all files that print or read | A console program. |
| 21.10 (time.h) | `utils.c` | Dates come from the clock. |
| Dir 4.6 (plain int/long) | everywhere | Kept for readability. |
| 15.5 (single exit) | `main.c`, a few input functions | An early `return` when input ends is easier to read. |

When you review a finding, either fix it or write the justification in a `# comment` above its line in
`cppcheck-suppressions.txt` (use the same id cppcheck prints, for example `misra-c2012-21.6:src/main.c`).
