# SurfaceRT_OS - top level build
#
#   make                          build for the Surface RT (Tegra 3, default)
#   make PLATFORM=tegra114        build for the Surface 2 / "Surface RT 2" (Tegra 4)
#   make image      only rebuild the images in dist/
#   make test       host unit tests (no ARM hardware needed)
#   make usb        complete USB package (image + flashers + unlock guide)
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

PYTHON   ?= python3
BUILD    ?= build
OUT      ?= dist
PLATFORM ?= tegra30
CMD      := $(PYTHON) scripts/build.py --build-dir $(BUILD) --out $(OUT) --platform $(PLATFORM)

.PHONY: all boot kernel services image images usb test sim clean doctor help

all: boot kernel services image

boot:
	$(CMD) boot

kernel:
	$(CMD) kernel

services:
	$(CMD) services

image: boot kernel services
	$(CMD) image

# Complete USB package: image + flashers + verifier + unlock guide, in
# $(OUT)/usb.  This is the "one folder" you copy to the machine that flashes.
usb: boot kernel services
	$(PYTHON) scripts/mkimage.py --build-dir $(BUILD) --out $(OUT) \
	        --platform $(PLATFORM) --usb-package

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
