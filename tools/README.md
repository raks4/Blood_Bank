# Static and dynamic analysis

    make check      # cppcheck on src/ (warnings, style, performance, portability, unused code)
    make analyze    # gcc -fanalyzer plus extra warnings (needs only gcc)
    make valgrind   # run the GoogleTest suite under Valgrind

## cppcheck
Install: `sudo apt install cppcheck` (version 2.x recommended). `make check` fails (exit code 1) on any
finding, so a clean run means nothing is left to explain.

## Valgrind
Install: `sudo apt install valgrind`. `make valgrind` builds the debug-enabled test suite and runs it
with full leak checks. Invalid memory accesses and definite or indirect leaks cause a nonzero exit code;
other leak categories are printed for inspection but do not fail the target.

## MISRA checks with cppcheck
cppcheck's MISRA addon needs the text of the MISRA rules, which is licensed and not included here.
Put your copy at `tools/misra_rules.txt` (the addon documents the format); `make check` then also runs
`--addon=tools/misra.json`. Without that file only the normal cppcheck checks run.

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
