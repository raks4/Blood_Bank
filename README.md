# Blood Bank

A console program in C for a blood bank: donors, donations, stock per blood group,
hospital requests, and password-protected users.

## Build and run (Linux)
    sudo apt install build-essential libssl-dev libgtest-dev cppcheck valgrind
    make
    make test
    make check       # cppcheck static analysis
    make valgrind    # run the test suite under Valgrind
    make helgrind    # check test threads for data races and synchronization errors
    make coverage    # run the tests with gcov and print line/branch coverage
    make clean       # optional: remove generated binaries and coverage reports
    make analyze     # optional: GCC static analysis
    ./bloodbank

Run it from the same folder every time: the data files are saved there (`donors.txt`, `batches.txt`,
`requests.txt`, `users.txt`). The first time, it asks you to create the first user. Actions are appended
to `audit.log` with a local timestamp and the acting username. Login attempts, logout, account and
password changes, donor/donation/inventory/request operations, and list or search views are recorded.
Passwords and donor names or contact details are not written to the audit log.
Audit events are queued for a dedicated logger thread and flushed before shutdown. While a user is
logged in, a maintenance thread checks hourly for inactive donors and expired blood batches; those
changes are saved and recorded in the audit log. Both workers are stopped cleanly on exit.

## Tests and coverage
Run the GoogleTest suite:

    make test

Build and run the tests with gcov instrumentation:

    make coverage

The coverage summary is printed in the terminal. Gcov also writes `*.gcov` detail files in the project
folder. Remove the generated test and coverage files with `make clean`.

## The files (read them in this order)
Every module has a header in `include/` (what it offers) and a source file in `src/` (how it works).

| Header (`include/`) | Source (`src/`) | What it is |
|---|---|---|
| `types.h` | - | The limits and rules, and every structure (`Donor`, `Batch`, `Request`, `User`, `Database`). Start here. |
| `donors.h` | `donors.c` | The linked list of donors: add, find, update details, deactivate, list, "may this donor give blood today?". |
| `blood.h` | `blood.c` | Batches (one donation = one batch), stock per blood group, and requests. |
| `users.h` | `users.c` | Login accounts. Passwords are stored only as a salted hash; accounts can be deleted by username. |
| `storage.h` | `storage.c` | Saving and loading the four text files. |
| `audit.h` | `audit.c` | Timestamped audit events in `audit.log`. |
| `utils.h` | `utils.c` | Safe keyboard input, dates, and small helpers. |
| - | `main.c` | The menus and `main()`. |

## How the data is organised
- Each kind of record is a **linked list**: a struct with a `next` pointer. `Database` holds the first
  node of each list. To search, the code walks the list with `while (current != NULL)`.
- The eight blood groups are an **array** of names; a group is stored as its position (0 to 7).
- Dates are stored as a number of seconds since 1970 (`long`).
- A donation becomes a **batch** with status *waiting for tests*. Staff mark it as tested before it counts as
  stock. Blood that is expired, discarded or still waiting is never given out.
- A request takes blood from the batches that **expire first**.

## The rules (all in `include/types.h`)
Donors must be 18 to 65 and wait 56 days between donations; a donation is 200 to 600 ml; blood lasts
35 days; stock under 1000 ml of a group is shown as LOW STOCK.

Active donors are automatically deactivated at startup and checked hourly while logged in after
180 days without a donation (or without activity since registration if they have never donated).
Deactivated donors are automatically deleted after 30 days. These month-based periods use 30-day
months. Blood batches older than their 35-day shelf life are marked expired and removed from stock,
with an hourly check while logged in. Staff can also delete a donor profile from
the Donors menu; the donor's batch and donation history remains stored by donor ID.
Older donor data files remain readable; donors that have never donated start their registration timer
when the old record is first loaded.

## About MISRA C
Written in the MISRA C:2012 style where it keeps the code readable: braces everywhere, no `goto`, no
unsafe string functions, every input length-checked, every `malloc` result checked, all memory freed.
It has **not** been run through a MISRA checker. Known deviations, kept on purpose to stay simple:
`malloc`/`free` for the linked lists, `stdio` and `time.h`, plain `int`/`long` instead of fixed-width
types, and a few early `return`s in the input-handling functions.

## Static analysis
`make check` runs cppcheck, and `make analyze` runs GCC's analyzer. `make valgrind` runs the memory-focused
tests under Valgrind (excluding the slower menu and password-hashing tests) to check for memory errors
and leaks. Install the analysis tools on Ubuntu with:

    sudo apt install cppcheck valgrind

Details and the expected MISRA findings are in `tools/README.md`.