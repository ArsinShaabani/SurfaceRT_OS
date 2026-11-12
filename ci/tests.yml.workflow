name: tests

on:
  push:
    branches: [ main ]
  pull_request:

jobs:
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Python unit tests (no toolchain needed)
        run: python3 tests/pytests/run_tests.py
      - name: elf2pe self test
        run: python3 tools/elf2pe.py --selftest
