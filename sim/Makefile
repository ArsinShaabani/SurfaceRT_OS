# SurfaceRT_OS - host simulator for the SRDP receive engine

CC      ?= cc
BUILD   ?= ../build
OUT     := $(BUILD)/sim

CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -DSRTOS_HOST \
          -I../kernel/include

SRCS := sim_main.c ../kernel/services/srdp_core.c ../kernel/lib/string.c \
        ../kernel/lib/vsnprintf.c

all: $(OUT)/srtos-sim

$(OUT)/srtos-sim: $(SRCS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(OUT):
	mkdir -p $(OUT)

run: $(OUT)/srtos-sim
	$(OUT)/srtos-sim --frames 3 --out /tmp

clean:
	rm -rf $(OUT)

.PHONY: all run clean
