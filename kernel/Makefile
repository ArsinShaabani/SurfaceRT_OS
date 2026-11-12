# SurfaceRT_OS - kernel build

CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size
PYTHON  ?= python3

PLATFORM ?= tegra30
BUILD    ?= ../build
OUT      := $(BUILD)/kernel

CFLAGS := -mcpu=cortex-a9 -marm -mfloat-abi=soft -mno-unaligned-access \
          -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
          -fno-unwind-tables -fno-asynchronous-unwind-tables \
          -ffunction-sections -fdata-sections -O2 -g \
          -Wall -Wextra -Wno-unused-parameter -Wno-address-of-packed-member \
          -Iinclude -Iplatform/$(PLATFORM)

LDFLAGS := -nostdlib -Wl,-T,linker.lds -Wl,--gc-sections -Wl,--emit-relocs \
           -Wl,--build-id=none -Wl,-Map=$(OUT)/kernel.map

SRCS := $(wildcard arch/arm/*.c) $(wildcard core/*.c) $(wildcard lib/*.c) \
        $(wildcard drivers/*/*.c) $(wildcard net/*.c) $(wildcard fs/*.c) \
        $(wildcard services/*.c) $(wildcard platform/$(PLATFORM)/*.c)
ASRCS := $(wildcard arch/arm/*.S)

OBJS := $(patsubst %.c,$(OUT)/%.o,$(SRCS)) $(patsubst %.S,$(OUT)/%.o,$(ASRCS))
DEPS := $(OBJS:.o=.d)

all: $(OUT)/KERNEL.ELF

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/KERNEL.ELF: $(OBJS) linker.lds
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	$(SIZE) $@
	@$(OBJDUMP) -h $@ | head -20

clean:
	rm -rf $(OUT)

-include $(DEPS)

.PHONY: all clean
