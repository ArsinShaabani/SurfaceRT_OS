# SurfaceRT_OS - top level build
#
#   make            build loader + kernel + modules + images
#   make image      only rebuild the images in dist/
#   make test       host unit tests (no ARM hardware needed)
#   make sim        host build of the portable core + display sink
#   make doctor     show the detected toolchain
#   make clean
#
# The work is done by scripts/build.py, which is plain Python and also runs on
# Windows (`py scripts\build.py all`), so there is exactly one place that knows
# the compiler flags:
#
#   Linux/macOS : ./toolchain/fetch-toolchain.sh  once, then make
#   Windows     : build.cmd      (or .\build.ps1)  once, then build.cmd

PYTHON ?= python3
BUILD  ?= build
OUT    ?= dist
CMD    := $(PYTHON) scripts/build.py --build-dir $(BUILD) --out $(OUT)

.PHONY: all boot kernel services image images test sim clean doctor help

all: boot kernel services image

boot:
	$(CMD) boot

kernel:
	$(CMD) kernel

services:
	$(CMD) services

image: boot kernel services
	$(CMD) image

images: image

test:
	$(PYTHON) scripts/build.py --build-dir $(BUILD) tests

sim:
	$(PYTHON) scripts/build.py --build-dir $(BUILD) sim

clean:
	$(PYTHON) scripts/build.py --build-dir $(BUILD) --out $(OUT) clean

doctor:
	$(PYTHON) scripts/build.py doctor

help:
	@sed -n '2,20p' Makefile
