# SurfaceRT_OS - UEFI loader
#
# Thin wrapper: the flags and the file list live in scripts/build.py so that the
# POSIX and Windows builds can never drift apart.

PYTHON ?= python3
BUILD  ?= ../build

all:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) boot

clean:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) clean

.PHONY: all clean
