# SurfaceRT_OS - loadable service modules (.SVC = position independent ELF)
#
# Thin wrapper around scripts/build.py: add <name>/<name>.c and it is picked up
# automatically, on every host OS.

PYTHON ?= python3
BUILD  ?= ../build

all:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) services

clean:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) clean

.PHONY: all clean
