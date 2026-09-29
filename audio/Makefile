# The audio core's firmware for Super Mirlo 64: the N64's audio RSP task (the
# ABI command lists src/audio/synthesis.c builds) and its AI, on Mirlo's audio
# core (rv32i + Zmmul, no FPU, no divider, no caches). Built on MIRLO's
# lang/c/audio runtime (start.S, nolibc.c, audio.ld, audio_hw.h, audio_cfu.h).
#
# Nothing here is in the bitstream: the game embeds the images through
# $(B)/audio_fw.h and loads them into the core at boot (hal/audio_hal.c).

MIRLO   ?= ../MIRLO
CROSS   ?= riscv-none-elf-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

RT       = $(MIRLO)/lang/c/audio
GEN_CSR  = $(MIRLO)/litex/build/litex/software/include/generated/csr.h
B       ?= ../build/audio

ARCHFLAGS = -march=rv32i_zmmul_zicsr -mabi=ilp32
CFLAGS    = $(ARCHFLAGS) -O2 -g -ffreestanding -nostdlib -fno-pic -mno-relax -fsigned-char \
            -fno-strict-aliasing -Wall -Wextra -ffunction-sections -fdata-sections -std=gnu11 \
            -DABI_STATE_CACHE -I. -I$(RT) -I$(B) $(CFLAGS_EXTRA)
LDFLAGS   = $(ARCHFLAGS) -nostdlib -Wl,--gc-sections -Wl,-Map,$(B)/audio.map -T $(RT)/audio.ld

OBJS = $(B)/start.o $(B)/abi_main.o $(B)/abi.o $(B)/nolibc.o

all: $(B)/audio_fw.h

$(B):
	mkdir -p $@

$(B)/generated_csr_addrs.h: $(GEN_CSR) | $(B)
	@echo "/* auto-extracted from csr.h -- do not edit */" > $@
	@echo "#ifndef AUDIO_GENERATED_CSR_ADDRS_H" >> $@
	@echo "#define AUDIO_GENERATED_CSR_ADDRS_H" >> $@
	@grep -E '#define (CSR_BASE|CSR_APF_AUDIO_)' $(GEN_CSR) >> $@
	@grep -E '#define CONFIG_CLOCK_FREQUENCY' $(dir $(GEN_CSR))soc.h >> $@
	@echo "#endif" >> $@

$(B)/start.o: $(RT)/start.S | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

# IMEM is 8 KiB: only the interpreter (abi.c, the hot code) is -O2; the
# task/AI loop and the libc helpers are -Os.
$(B)/abi_main.o $(B)/nolibc.o: CFLAGS := $(subst -O2,-Os,$(CFLAGS))
$(B)/nolibc.o: $(RT)/nolibc.c $(B)/generated_csr_addrs.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: %.c $(B)/generated_csr_addrs.h abi.h abi_mailbox.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/audio.elf: $(OBJS) $(RT)/audio.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	$(SIZE) $@

$(B)/audio_imem.bin: $(B)/audio.elf
	$(OBJCOPY) -O binary -j .text $< $@

$(B)/audio_dmem.bin: $(B)/audio.elf
	$(OBJCOPY) -O binary -j .rodata -j .data $< $@

# Both images as C arrays of little-endian words, for the game firmware.
$(B)/audio_fw.h: $(B)/audio_imem.bin $(B)/audio_dmem.bin
	python3 $(MIRLO)/tools/bin2words.py audio_imem $(B)/audio_imem.bin audio_dmem $(B)/audio_dmem.bin > $@

clean:
	rm -rf $(B)

.PHONY: all clean
