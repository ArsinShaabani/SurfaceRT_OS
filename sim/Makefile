# SurfaceRT_OS - host simulator for the SRDP receive engine

PYTHON ?= python3
BUILD  ?= ../build
SIM    := $(BUILD)/sim/srtos-sim

all:
	$(PYTHON) ../scripts/build.py --build-dir $(BUILD) sim

run: all
	$(SIM) --frames 3 --out /tmp

clean:
	rm -rf $(BUILD)/sim

.PHONY: all run clean
