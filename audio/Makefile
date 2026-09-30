# The audio core's firmware for Super Mirlo 64: the N64's audio RSP task (the
# ABI command lists src/audio/synthesis.c builds) and its AI, on Mirlo's audio
# core (rv32i + Zmmul, no FPU, no divider, no caches; with CPU=mips, MIRLO's
# MIPS SoC's mips_lite). Built on MIRLO's lang/c/audio runtime (start.S,
# nolibc.c, audio.ld, audio_hw.h, audio_cfu.h).
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

START = start
LDSCRIPT = $(RT)/audio.ld
LINK  = $(CC) $(LDFLAGS)

# CPU=mips: MIRLO's MIPS SoC (its branch `mips`): the audio core is mips_lite
# (rtl/mips/mips_lite.sv), little-endian, the CFU as MIPS opcode 0x1F.
ifeq ($(CPU),mips)
include $(MIRLO)/lang/mips/mips.mk
GEN_CSR   = $(MIRLO)/lang/mips/include/generated/csr.h
CC        = $(MIPS_GCC)
OBJCOPY   = $(MIPS_BIN)objcopy
SIZE      = $(MIPS_BIN)size
ARCHFLAGS = $(MIPS_LITE_CFLAGS) -fno-store-merging -fno-tree-loop-distribute-patterns -I$(MIRLO)/lang/c/mips
CFLAGS    = $(ARCHFLAGS) -O2 -g -ffreestanding -nostdlib -fsigned-char \
            -fno-strict-aliasing -Wall -Wextra -ffunction-sections -fdata-sections -std=gnu11 \
            -DABI_STATE_CACHE -I. -I$(RT) -I$(B) $(CFLAGS_EXTRA)
START     = start_mips
LDSCRIPT  = $(RT)/audio_mips.ld
LINK      = $(MIPS_LD) -EL --gc-sections -Map=$(B)/audio.map -T $(LDSCRIPT)
endif

OBJS = $(B)/$(START).o $(B)/abi_main.o $(B)/abi.o $(B)/nolibc.o

all: $(B)/audio_fw.h

$(B):
	mkdir -p $@

$(B)/generated_csr_addrs.h: $(GEN_CSR) | $(B)
	@echo "/* auto-extracted from csr.h -- do not edit */" > $@
	@echo "#ifndef AUDIO_GENERATED_CSR_ADDRS_H" >> $@
	@echo "#define AUDIO_GENERATED_CSR_ADDRS_H" >> $@
	@echo "#define CSR_BASE 0xf0000000L" >> $@
	@grep -E '#define CSR_APF_AUDIO_' $(GEN_CSR) >> $@
	@grep -E '#define CONFIG_CLOCK_FREQUENCY' $(dir $(GEN_CSR))soc.h >> $@
	@echo "#endif" >> $@

$(B)/$(START).o: $(RT)/$(START).S | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

# IMEM is 8 KiB: only the interpreter (abi.c, the hot code) is -O2; the
# task/AI loop and the libc helpers are -Os.
$(B)/abi_main.o $(B)/nolibc.o: CFLAGS := $(subst -O2,-Os,$(CFLAGS))
$(B)/nolibc.o: $(RT)/nolibc.c $(B)/generated_csr_addrs.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: %.c $(B)/generated_csr_addrs.h abi.h abi_mailbox.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/audio.elf: $(OBJS) $(LDSCRIPT)
	$(LINK) -o $@ $(OBJS)
ifeq ($(CPU),mips)
	python3 $(MIRLO)/lang/c/mips/check_isa.py $(MIPS_BIN)objcopy $@ .text || { rm -f $@; false; }
endif
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
