# SurfaceRT_OS - UEFI loader (BOOTARM.EFI)

CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size
PYTHON  ?= python3

BUILD   ?= ../build
OUT     := $(BUILD)/boot

CFLAGS := -mcpu=cortex-a9 -marm -mfloat-abi=soft -mno-unaligned-access \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables \
          -fno-asynchronous-unwind-tables -fshort-wchar -fPIC -fvisibility=hidden \
          -fno-plt -ffunction-sections -fdata-sections -Os -g \
          -Wall -Wextra -Wno-unused-parameter -Wno-address-of-packed-member \
          -I. -I../kernel/include

LDFLAGS := -nostdlib -shared -Bsymbolic -Wl,--no-undefined -Wl,-e,efi_main \
           -Wl,-T,loader.lds

SRCS := loader.c elfload.c ../kernel/lib/vsnprintf.c ../kernel/lib/string.c
OBJS := $(patsubst %.c,$(OUT)/%.o,$(notdir $(SRCS)))

all: $(OUT)/BOOTARM.EFI

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: ../kernel/lib/%.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/boot.elf: $(OBJS) loader.lds
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	$(SIZE) $@

$(OUT)/BOOTARM.EFI: $(OUT)/boot.elf
	$(PYTHON) ../tools/elf2pe.py $< $@

clean:
	rm -rf $(OUT)


