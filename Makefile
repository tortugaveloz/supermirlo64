# Super Mirlo 64: Super Mario 64 (the n64decomp/sm64 decompilation) on Mirlo.
#
#   make                -> build/super-mirlo-64.bin, the game file for the SD card
#
# Needs (see README.md):
#   - MIRLO/ built: litex (twice, around lang/c/geom) -- csr.h, the LiteX
#     libraries, and the geometry core's firmware that the bitstream carries;
#   - decomp/ with its assets extracted from YOUR ROM (make assets);
#   - the RISC-V GCC on PATH.

ifeq ($(filter-out 3.%,$(MAKE_VERSION)),)
$(error GNU Make 4.0 or later is required (this is $(MAKE_VERSION)); on macOS: brew install make, then run gmake)
endif

SM64_DIR     ?= decomp
SM64_ROM     ?= baserom.us.z64
SM64_VERSION := us
MIRLO        ?= MIRLO

SM64_DIR_ABS := $(abspath $(SM64_DIR))
FAKE_MIPS_BIN := $(abspath tools/fake-mips-toolchain)

OUTPUT_DIRECTORY = build

# ---- LiteX toolchain plumbing (as MIRLO's lang/c/examples) ------------------
LITEX_ROOT_DIRECTORY = $(MIRLO)/litex
BUILD_DIR        = $(LITEX_ROOT_DIRECTORY)/build/litex
SOC_DIRECTORY    = $(LITEX_ROOT_DIRECTORY)/vendor/litex/litex/soc
LINKER_DIRECTORY = $(MIRLO)/lang/linker
MIRLO_C          = $(MIRLO)/lang/c

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

# LiteX's common.mak compiles everything -Os. The game's code lives in SDRAM,
# where size costs nothing, and -O2 is faster.
CFLAGS := $(filter-out -Os,$(CFLAGS)) -O2
# The host simulator (sim/) is the reference, so both builds must round and
# alias the same way:
#  - no fused multiply-add (rv32f has FMADD.S, x86-64 baseline does not);
#  - char is signed, as on MIPS and x86-64 (rv32 defaults to unsigned);
#  - no type-based alias analysis: the decomp was written for IDO, which has
#    none, and relies on it (mtxf_mul() copies a float temp through a u32 *;
#    with strict aliasing GCC deletes the stores and every matrix is garbage).
CFLAGS += -ffp-contract=off -fsigned-char -fno-strict-aliasing

# The geometry core's firmware is in the bitstream; frame.c reads one of its
# variables (triangles emitted) straight from its RAM. Its address comes from
# the geom.elf the running bitstream was built with.
NM       := $(TARGET_PREFIX)nm
GEOM_ELF ?= $(MIRLO_C)/geom/build/geom.elf
GEOM_TRIS_EMITTED_ADDR := $(shell $(NM) $(GEOM_ELF) 2>/dev/null | sed -n 's/^\([0-9a-f]*\) [BbDd] geom_tris_emitted$$/0x\1u/p')
ifeq ($(GEOM_TRIS_EMITTED_ADDR),)
$(error cannot find geom_tris_emitted in $(GEOM_ELF) -- build $(MIRLO_C)/geom first)
endif
CFLAGS += -DGEOM_TRIS_EMITTED_ADDR=$(GEOM_TRIS_EMITTED_ADDR)

# ---- the decomp ----------------------------------------------------------------
SM64_INCLUDES := -I$(SM64_DIR_ABS)/include -I$(SM64_DIR_ABS)/build/$(SM64_VERSION) \
                 -I$(SM64_DIR_ABS)/build/$(SM64_VERSION)/include -I$(SM64_DIR_ABS)/src -I$(SM64_DIR_ABS)
SM64_DEFINES  := -DVERSION_US=1 -D_LANGUAGE_C -DNON_MATCHING=1 -DAVOID_UB=1 \
                 -DF3DEX_GBI_2=1 -DF3DEX_GBI_SHARED=1
#  - NO_SEGMENTED_MEMORY: segmented_to_virtual() is the identity and
#    load_segment*() do nothing -- everything is linked into one image.
#  - include/ comes first, so its segments.h replaces the decomp's
#    N64-RDRAM layout (the main pool lives in SDRAM).
GAME_INCLUDES := -I$(CURDIR)/include $(SM64_INCLUDES) -I$(CURDIR)/f3d -I$(CURDIR)/hal \
                 -I$(CURDIR)/audio -I$(MIRLO_C)/game -I$(MIRLO_C)/geom -I$(MIRLO_C)/audio
GAME_DEFINES  := $(SM64_DEFINES) -DNO_SEGMENTED_MEMORY -DPORT_FULL_GAME

# The upstream Makefile's own C sources (src/, levels/, the libultra float
# helpers lib/src/gu*.c) and data (actors, behaviours, the generated assets).
# The rest of lib/src drives N64 hardware and is replaced by hal/.
SM64_C_FILES := $(wildcard $(SM64_DIR_ABS)/src/game/*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/engine/*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/audio/*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/menu/*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/buffers/*.c) \
                $(wildcard $(SM64_DIR_ABS)/levels/*/leveldata.c) \
                $(wildcard $(SM64_DIR_ABS)/levels/*/script.c) \
                $(wildcard $(SM64_DIR_ABS)/levels/*/geo.c) \
                $(wildcard $(SM64_DIR_ABS)/lib/src/gu*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/goddard/*.c) \
                $(wildcard $(SM64_DIR_ABS)/src/goddard/dynlists/*.c) \
                $(wildcard $(SM64_DIR_ABS)/actors/*.c) \
                $(SM64_DIR_ABS)/data/behavior_data.c \
                $(wildcard $(SM64_DIR_ABS)/bin/*.c) \
                $(SM64_DIR_ABS)/levels/scripts.c \
                $(SM64_DIR_ABS)/build/$(SM64_VERSION)/assets/mario_anim_data.c \
                $(SM64_DIR_ABS)/build/$(SM64_VERSION)/assets/demo_data.c \
                $(wildcard $(SM64_DIR_ABS)/build/$(SM64_VERSION)/bin/*_skybox.c)
SM64_OBJECTS := $(patsubst $(SM64_DIR_ABS)/%.c,$(OUTPUT_DIRECTORY)/sm64/%.o,$(SM64_C_FILES))

# ---- our code ------------------------------------------------------------------
LOCAL_C := platform/game_main.c platform/port_boot.c platform/fps_overlay.c \
           hal/os_hal.c hal/os_thread_hal.c hal/controller.c hal/sptask_dualcore.c \
           hal/exec_dl_wrap.c hal/rsp_stubs.c hal/fastmem.c hal/audio_hal.c hal/mirlo_game.c \
           f3d/f3d_emit.c \
           $(MIRLO_C)/game/frame.c $(MIRLO_C)/game/log.c $(MIRLO_C)/game/audio_load.c
LOCAL_OBJECTS := $(addprefix $(OUTPUT_DIRECTORY)/local/,$(notdir $(LOCAL_C:.c=.o)))
vpath %.c platform hal f3d $(MIRLO_C)/game
VPATH += $(LINKER_DIRECTORY)

# Engine functions the port takes over with --wrap:
#  - exec_display_list: runs the graphics task inline (hal/exec_dl_wrap.c);
#  - end_master_display_list: the FPS overlay (platform/fps_overlay.c).
comma := ,
WRAP_FLAGS := $(addprefix -Wl$(comma)--wrap=,exec_display_list end_master_display_list)

# The sound data is not in the image: it is its own block of the game file
# (SND0), loaded at boot; build/sound_le/sound_syms.ld pins its symbols.
SOUND_DIR  := $(OUTPUT_DIRECTORY)/sound_le
SOUND_SYMS := $(CURDIR)/$(SOUND_DIR)/sound_syms.ld
AUDIO_FW_H := $(CURDIR)/$(OUTPUT_DIRECTORY)/audio/audio_fw.h

.PHONY: all assets clean
all: $(OUTPUT_DIRECTORY)/super-mirlo-64.bin

# ---- step 1: the decomp's generated sources, from your ROM ---------------------
# extract_assets.py pulls the assets out of the ROM; the decomp's own Makefile,
# run with a fake MIPS toolchain on PATH (tools/fake-mips-toolchain), then
# generates the architecture-independent sources (text, level headers,
# textures as C, animation and demo data, skyboxes). Its MIPS link fails at
# the end, as expected: only the generated files are wanted.
# The generated files the main build needs; `make assets` checks they exist.
SM64_B := $(SM64_DIR_ABS)/build/$(SM64_VERSION)
SM64_SKYBOXES := $(addprefix build/$(SM64_VERSION)/bin/,$(addsuffix _skybox.c,$(notdir $(basename $(wildcard $(SM64_DIR_ABS)/textures/skyboxes/*.png)))))
ASSET_REQUIRED := $(SM64_B)/assets/mario_anim_data.c $(SM64_B)/assets/demo_data.c \
                  $(SM64_B)/include/text_strings.h $(SM64_B)/include/level_headers.h \
                  $(addprefix $(SM64_DIR_ABS)/,$(SM64_SKYBOXES))
# nproc is GNU-only; macOS has sysctl
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
ASSETS_LOG := $(CURDIR)/$(OUTPUT_DIRECTORY)/assets.log

assets:
	@if [ ! -e "$(SM64_DIR_ABS)/baserom.$(SM64_VERSION).z64" ]; then \
		ln -s "$(abspath $(SM64_ROM))" "$(SM64_DIR_ABS)/baserom.$(SM64_VERSION).z64"; \
	fi
	@mkdir -p $(dir $(ASSETS_LOG))
	cd $(SM64_DIR_ABS) && python3 extract_assets.py $(SM64_VERSION)
	@echo "Generating the decomp's sources (log: $(ASSETS_LOG))"
	-@cd $(SM64_DIR_ABS) && PATH="$(FAKE_MIPS_BIN):$$PATH" $(MAKE) VERSION=$(SM64_VERSION) -k -j$(JOBS) \
		build/$(SM64_VERSION)/assets/mario_anim_data.c \
		build/$(SM64_VERSION)/assets/demo_data.c \
		build/$(SM64_VERSION)/include/text_strings.h \
		build/$(SM64_VERSION)/include/level_headers.h \
		$(SM64_SKYBOXES) \
		> $(ASSETS_LOG) 2>&1
	-@cd $(SM64_DIR_ABS) && PATH="$(FAKE_MIPS_BIN):$$PATH" $(MAKE) VERSION=$(SM64_VERSION) -k -j$(JOBS) \
		>> $(ASSETS_LOG) 2>&1
	@missing=0; for f in $(ASSET_REQUIRED); do \
		if [ ! -s "$$f" ]; then echo "make assets: missing generated file: $$f"; missing=1; fi; \
	done; \
	if [ $$missing = 1 ]; then \
		echo "--- last lines of $(ASSETS_LOG) ---"; grep -iE 'error|not found|No such' $(ASSETS_LOG) | head -20; \
		echo "(the final MIPS link failing is expected; the files above are not)"; exit 1; \
	fi; echo "make assets: all generated files present"

# ---- step 2: the sound data, and the audio core's firmware ---------------------
$(SOUND_SYMS):
	SM64_DIR=$(SM64_DIR_ABS) OUT=$(CURDIR)/$(SOUND_DIR) ./tools/build_sound_le.sh
$(SOUND_DIR)/sound.bin: $(SOUND_SYMS)
$(AUDIO_FW_H): $(wildcard audio/*.c audio/*.h)
	$(MAKE) -C audio MIRLO=$(abspath $(MIRLO)) B=$(abspath $(OUTPUT_DIRECTORY)/audio)

# ---- step 3: compile and link ------------------------------------------------------
$(OUTPUT_DIRECTORY)/sm64/%.o: CFLAGS += $(GAME_INCLUDES) $(GAME_DEFINES) -w
# Files that only draw (shadows, skybox, dialog boxes, transitions, snow,
# moving textures, text, HUD): their double literals would promote the maths
# to soft-float double on this single-precision CPU. Single-precision
# constants there change pixels at most, never game state -- physics and
# object logic keep the N64's doubles (the attract demos replay exact inputs).
RENDER_ONLY_SP := shadow skybox ingame_menu screen_transition envfx_snow moving_texture print hud
$(addprefix $(OUTPUT_DIRECTORY)/sm64/src/game/,$(addsuffix .o,$(RENDER_ONLY_SP))): CFLAGS += -fsingle-precision-constant
$(OUTPUT_DIRECTORY)/sm64/%.o: $(SM64_DIR_ABS)/%.c
	@mkdir -p $(dir $@)
	$(compile)

$(OUTPUT_DIRECTORY)/local/%.o: CFLAGS += $(GAME_INCLUDES) $(GAME_DEFINES) -w
# memcpy/memset themselves: at -O2 GCC turns their loops back into calls to
# memcpy/memset (-ftree-loop-distribute-patterns), i.e. into themselves
$(OUTPUT_DIRECTORY)/local/fastmem.o: CFLAGS += -fno-tree-loop-distribute-patterns
$(OUTPUT_DIRECTORY)/local/audio_hal.o: $(AUDIO_FW_H)
$(OUTPUT_DIRECTORY)/local/audio_hal.o: CFLAGS += -DAUDIO_FW_HEADER='"$(AUDIO_FW_H)"'
$(OUTPUT_DIRECTORY)/local/%.o: %.c
	@mkdir -p $(dir $@)
	$(compile)

$(OUTPUT_DIRECTORY)/init_asm.o: init_asm.S
	@mkdir -p $(dir $@)
	$(assemble)

# One contiguous image from 0x40000000 (linker/regions.ld).
$(OUTPUT_DIRECTORY)/super-mirlo-64.elf: $(OUTPUT_DIRECTORY)/init_asm.o $(SM64_OBJECTS) $(LOCAL_OBJECTS) \
		linker/c-linker.ld linker/regions.ld $(SOUND_SYMS)
	$(CC) $(LDFLAGS) -L $(CURDIR)/linker -L $(LINKER_DIRECTORY) -T $(CURDIR)/linker/c-linker.ld -N -o $@ \
		$(OUTPUT_DIRECTORY)/init_asm.o $(SM64_OBJECTS) $(LOCAL_OBJECTS) $(SOUND_SYMS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--gc-sections $(WRAP_FLAGS) -Wl,-Map,$@.map \
		$(LIBS:lib%=-l%) -lm -lgcc
	chmod -x $@

$(OUTPUT_DIRECTORY)/super-mirlo-64-program.bin: $(OUTPUT_DIRECTORY)/super-mirlo-64.elf
	$(OBJCOPY) -O binary $< $@
	chmod -x $@

# ---- step 4: the game file -----------------------------------------------------------
# The program, the sound data as block SND0, and the footer the BIOS and
# hal/mirlo_game.c read; the header asks for 320 x 240, the N64's own.
$(OUTPUT_DIRECTORY)/super-mirlo-64.bin: $(OUTPUT_DIRECTORY)/super-mirlo-64-program.bin $(SOUND_DIR)/sound.bin
	python3 $(MIRLO)/tools/make_mirlo_game.py --video 320 $< $@ SND0=$(SOUND_DIR)/sound.bin
	@echo "Game file: $@ -> SD card Assets/mirlo/common/"

-include $(SM64_OBJECTS:.o=.d)
-include $(LOCAL_OBJECTS:.o=.d)

clean:
	rm -rf $(OUTPUT_DIRECTORY)
