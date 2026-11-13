# SurfaceRT_OS - kernel
#
# Thin wrapper around scripts/build.py (see boot/Makefile for the reason).

PYTHON ?= python3
BUILD  ?= ../build
PLATFORM ?= tegra30

all:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) kernel --platform $(PLATFORM)

clean:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) clean

.PHONY: all clean
