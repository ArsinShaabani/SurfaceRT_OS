# SurfaceRT_OS - host unit tests (C + Python)

PYTHON ?= python3
BUILD  ?= ../build

all:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) tests

run: all

clean:
	rm -rf $(BUILD)/tests

.PHONY: all run clean
