# SurfaceRT_OS - top level build
#
#   make            build loader + kernel + modules and the boot images
#   make image      only rebuild the images in dist/
#   make test       host unit tests (no ARM hardware needed)
#   make sim        host build of the portable core + display sink
#   make clean
#
# No SDK required: `make` uses an arm-none-eabi-gcc found on PATH, or the pinned
# toolchain that ./toolchain/fetch-toolchain.sh installs into .toolchain/.

PYTHON  ?= python3
BUILD   ?= build
OUT     ?= dist

# --- toolchain discovery -----------------------------------------------------
TOOLCHAIN_DIR := $(abspath $(firstword $(wildcard .toolchain/xpack-arm-none-eabi-gcc-*/bin)))
ifeq ($(origin CROSS_COMPILE),undefined)
  ifneq ($(TOOLCHAIN_DIR),)
    CROSS_COMPILE := $(TOOLCHAIN_DIR)/arm-none-eabi-
  else ifneq ($(shell command -v arm-none-eabi-gcc 2>/dev/null),)
    CROSS_COMPILE := arm-none-eabi-
  else
    CROSS_COMPILE := missing-
  endif
endif

export CROSS_COMPILE
export BUILD := $(abspath $(BUILD))

.PHONY: all boot kernel services image images test sim clean distclean help check-toolchain

all: check-toolchain boot kernel services image

check-toolchain:
	@if [ "$(CROSS_COMPILE)" = "missing-" ]; then \
	 echo "error: no arm-none-eabi-gcc found."; \
	 echo "       run ./toolchain/fetch-toolchain.sh once (downloads a pinned,"; \
	 echo "       self-contained toolchain into .toolchain/) or install gcc-arm-none-eabi"; \
	 echo "       from your distribution and re-run make."; \
	 exit 1; \
	fi
	@$(CROSS_COMPILE)gcc --version | head -1

boot: check-toolchain
	$(MAKE) -C boot

kernel: check-toolchain
	$(MAKE) -C kernel

services: check-toolchain
	$(MAKE) -C services

image: boot kernel services
	$(PYTHON) scripts/mkimage.py --build-dir $(BUILD) --out $(OUT)

images: image

test:
	$(MAKE) -C tests run

sim:
	$(MAKE) -C sim

clean:
	rm -rf $(BUILD) $(OUT)
	$(MAKE) -C tests clean || true
	$(MAKE) -C sim clean || true

help:
	@sed -n "2,14p" Makefile
