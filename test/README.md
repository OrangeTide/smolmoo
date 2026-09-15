# C unit tests

TAP-based unit tests for the parts of smolmoo that run below the HTTP
integration suite in `../test.sh`, starting with the embedded CPU core.

## taptest

The harness is vendored from taptest:

    upstream: http://github.com/OrangeTide/taptest

- `taptest.c`, `taptest.h`, `taptest_selftest.c` build the driver, which runs
  each test binary and reads its output as TAP.
- `test.h`, `testmain.c` are the unit-test library. A test is a `test_NAME.c`
  file that defines a `tap_cases[]` table and no `main`; `testmain.c` supplies
  the `main` and the TAP reporting.

## Running

    make ctest

builds the driver and the unit tests, runs the driver self-test, then runs the
tests. `make test` runs these before the HTTP integration suite.

## Adding a test

Copy `test_rv32.c` as a template: include `test.h`, write cases that use
`TAP_CHECK`/`TAP_ASSERT`, list them in `tap_cases[]`, then add a build rule in
the top-level Makefile.
