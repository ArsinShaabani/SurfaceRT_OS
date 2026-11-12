# SurfaceRT_OS - loadable service modules (.SVC = position independent ELF)

CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
BUILD   ?= ../build
OUT     := $(BUILD)/services

CFLAGS := -mcpu=cortex-a9 -marm -mfloat-abi=soft -mno-unaligned-access \
          -ffreestanding -fno-builtin -fPIC -fvisibility=hidden -fno-plt \
          -fno-stack-protector -fno-unwind-tables -ffunction-sections \
          -fdata-sections -Os -g -Wall -Wextra -Wno-unused-parameter \
          -I../kernel/include

LDFLAGS := -nostdlib -shared -Bsymbolic -Wl,-e,srt_module_init \
           -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,notext

MODULES := hello

all: $(patsubst %,$(OUT)/%.SVC,$(MODULES))

# every module is linked from its own source plus the freestanding string code
define MODULE_rule
$(OUT)/$(1).SVC: $(1)/$(1).c ../kernel/lib/string.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $$@ $$^ -lgcc
	@echo "  module $$@"
endef

$(foreach m,$(MODULES),$(eval $(call MODULE_rule,$(m))))

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all clean
