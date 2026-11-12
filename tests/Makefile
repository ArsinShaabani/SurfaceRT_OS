# SurfaceRT_OS - host unit tests

CC     ?= cc
BUILD  ?= ../build
OUT    := $(BUILD)/tests

CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -DSRTOS_HOST \
          -I../kernel/include

SRCS := test_main.c ../kernel/lib/string.c ../kernel/lib/vsnprintf.c \
        ../kernel/core/heap.c ../kernel/services/srdp_core.c ../boot/elfload.c

all: $(OUT)/srtos-tests

$(OUT)/srtos-tests: $(SRCS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(OUT):
	mkdir -p $(OUT)

run: $(OUT)/srtos-tests
	$(OUT)/srtos-tests ../build/kernel/KERNEL.ELF
	$(PYTHON) pytests/run_tests.py

clean:
	rm -rf $(OUT)

.PHONY: all run clean
