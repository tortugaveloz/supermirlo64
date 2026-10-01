# Super Mirlo 64

Super Mario 64 on [Mirlo](https://github.com/tortugaveloz/MIRLO), a computer for the Analogue Pocket inspired by the N64 hardware.

> **Experimental branch (`mips`):** this branch builds the game for Mirlo's experimental MIPS version (MIRLO's branch `mips`, where all three cores are MIPS and LiteX is gone). Use it with that core; for the released RISC-V Mirlo, use branch `main`.

It is a port of SM64, based on the [n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation. The engine is compiled unchanged; this repository adds:
* a libultra layer (`hal/`);
* a translator from the display lists the engine builds to Mirlo's display lists (`f3d/`). The original game uses the N64's Fast3D microcode; the decomp is built here with its F3DEX2 command encoding, and that is what the translator reads;
* the N64 audio microcode as the audio core's firmware (`audio/`).

The game CPU runs the game, Mirlo's geometry core does what the N64's RSP did (transform, lighting, clipping, triangle setup), and MRDP draws. It runs at the N64's own 320 × 240.

**This repository contains none of the game's code or assets.** You need your own copy of the game, from which the build extracts the assets.

## Contents

| | |
|---|---|
| `MIRLO/` | the Mirlo core and SDK (submodule) |
| `decomp/` | n64decomp/sm64 (submodule), unmodified |
| `hal/` | libultra on Mirlo: threads, message queues, timing, controller, EEPROM save, audio, the graphics task |
| `f3d/` | F3DEX2 → GDL translator, with a translation cache for static display lists |
| `audio/` | the audio core's firmware: the N64 audio microcode's commands and the AI |
| `platform/` | `main()`, the boot script, the FPS overlay |
| `include/`, `linker/` | the memory layout |
| `tools/` | the sound-data build, the fake MIPS toolchain the decomp's Makefile needs to generate its assets |
| `sim/` | a host simulator: the whole game on a Linux PC, rendered through the same pipeline into images |

## Building the game file (Linux)

The following steps assume Ubuntu 24.04 (x86-64). Notes for [macOS](#building-on-macos) and [Arch Linux](#building-on-arch-linux) are at the end of this section. Use **GNU Make 4.0 or later** everywhere below (the decomp's Makefile needs it); on macOS that is `gmake`.

### 1. Tools

```bash
sudo apt install build-essential git python3 python3-venv pkgconf gcc-mips-linux-gnu binutils-mips-linux-gnu
```

The MIPS GCC builds code for all three of Mirlo's cores: the flags (`MIRLO/lang/mips/mips.mk`) choose the byte order and the ABI. Another MIPS GCC works too: set `MIPS_CC` and `MIPS_BIN` (the tools' prefix).

**Python 3.10 or later**, with **meson** and **ninja** (to build picolibc for the cores):

```bash
python3 -m venv ~/.venvs/mirlo && source ~/.venvs/mirlo/bin/activate
pip install meson ninja
```

### 2. Sources

```bash
git clone --recursive -b mips https://github.com/tortugaveloz/supermirlo64.git
cd supermirlo64
git submodule update --init --recursive    # in case the clone left submodules unchecked-out
```

### 3. Mirlo

This builds the C library and runtime for the cores, and the geometry core's firmware:

```bash
(cd MIRLO/lang/mips && make -f lib.mk VARIANT=game && make -f lib.mk VARIANT=lite)
(cd MIRLO/lang/c/geom && make)
```

The Pocket needs the Mirlo core, whose bitstream you build with Quartus (see `MIRLO/README.md`). The geometry core's firmware is part of that bitstream, and the game reads one of its variables. So build the game against the same `MIRLO/lang/c/geom/build/geom.elf` the bitstream was built with, or pass `GEOM_ELF=<that geom.elf>` to `make` below. With Ubuntu 24.04's `gcc-mips-linux-gnu` (GCC 13) the variable is at `0x20008030`:

```bash
mips-linux-gnu-nm MIRLO/lang/c/geom/build/geom.elf | grep geom_tris_emitted    # 20008030 B geom_tris_emitted
```

### 4. Your ROM

Copy your Super Mario 64 (USA) ROM, in big-endian `.z64` format, to `baserom.us.z64` in this directory. Its SHA-1 must be `9bef1128717f958171a4afac3ed78ee2bb4e86ce`:

```bash
sha1sum baserom.us.z64
```

### 5. Build

```bash
make assets      # extract the assets from the ROM and generate the decomp's sources
make -j$(nproc)  # -> build/super-mirlo-64.bin
```

`make assets` runs the decomp's own asset extraction and generators. Its MIPS build ends with errors, as expected: only the generated sources are used. The decomp's output goes to `build/assets.log`, and at the end `make assets` checks that every generated file the main build needs exists (`text_strings.h`, `level_headers.h`, `mario_anim_data.c`, `demo_data.c` and the skybox sources). If one is missing it lists it, prints the relevant errors from the log, and fails.

### 6. On the Pocket

With the Mirlo core installed (`MIRLO/README.md`), copy the game file to the SD card:

```
Assets/mirlo/common/Super Mirlo 64.bin
```

Launch **Mirlo** (under Computer) and pick the game. Saves go to `Saves/mirlo/common/` when you quit the game.

**Controls:**

| Pocket | N64 |
|---|---|
| D-pad | control stick |
| A, B | A, B |
| X, Y | C-right, C-left |
| L | Z |
| R | R |
| Start | Start |
| Select | C-down |

C-up is unassigned by default, but it can be assigned through the Pocket Controls menu.

More control options are included in the core settings (Pocket menu → Core Settings):
* Start can be mapped to the combination Select+Start. That would free the Start button for something else.
* R can be chosen to work as a modifier. In that mode, while R is pressed, X/B/Y/A become the four C buttons. In addition, the D-Pad works as a joystick press at 50%.
* The stick and N64 D-pad sources can also be configured. The analog sources (L stick, R stick) need a Dock controller that the Pocket reports as analog; without one, L stick falls back to the D-pad.
* L (the N64 L button) is unassigned by default; it can be set to a Dock controller's L2, R2, L3 or R3.

## Building on macOS

Tested on macOS 26, Apple silicon, with Homebrew. The build is the same as above, with these differences:

```bash
xcode-select --install                 # clang, git, make 3.81 (too old, see below)
brew install make python@3.12          # gmake 4.x; Python
```

* **GNU Make:** Apple ships make 3.81, which cannot run the decomp's Makefile (it uses `!=`, from 4.0). Use `gmake` instead of `make` in every step, or put Homebrew's `gnubin` first on your `PATH`: `export PATH="$(brew --prefix make)/libexec/gnubin:$PATH"`. The top-level Makefile stops with a message if it is run by make 3.81.
* **MIPS GCC:** any MIPS cross GCC with its binutils (a `mips-elf` or `mips-linux-gnu` build); set `MIPS_CC` (the compiler) and `MIPS_BIN` (the binutils' prefix, e.g. `/opt/mips/bin/mips-elf-`). Not yet tested on macOS for this branch.
* **Python:** `python3.12 -m venv ~/.venvs/mirlo`, then the `pip install` from step 1. Newer versions work too.
* **ROM checksum:** `shasum baserom.us.z64` instead of `sha1sum`.
* **Job count:** the Makefile finds it with `sysctl` when `nproc` does not exist; use `-j$(sysctl -n hw.ncpu)` for the main build.

The whole sequence:

```bash
export PATH="$(brew --prefix make)/libexec/gnubin:$PATH"
export MIPS_CC=/path/to/mips-gcc MIPS_BIN=/path/to/mips-   # your MIPS cross toolchain
source ~/.venvs/mirlo/bin/activate
git submodule update --init --recursive
(cd MIRLO/lang/mips && make -f lib.mk VARIANT=game && make -f lib.mk VARIANT=lite)
(cd MIRLO/lang/c/geom && make)
cp /path/to/your.z64 baserom.us.z64
make assets && make -j$(sysctl -n hw.ncpu)
```

The bundled `armips` and the decomp's other host tools build with Apple's clang. The `ido-static-recomp` error during `make assets` is part of the expected MIPS failure.

## Building on Arch Linux

Arch, CachyOS and similar distributions ship newer GCC and Python than Ubuntu 24.04:

```bash
sudo pacman -S --needed base-devel git python cmake ninja pkgconf
```

* **Python:** the system Python works (tested up to 3.14), with meson and ninja in the venv.
* **armips:** if the bundled `decomp/tools/armips.cpp` fails to compile with your GCC (errors such as `exponent has no digits` or missing `uint8_t`/`INT64_C`), build a current [Kingcom/armips](https://github.com/Kingcom/armips) (0.11 or later; there is no Arch package) and put it on your `PATH`. The decomp's Makefile uses a system `armips` when it finds one:

  ```bash
  git clone --recursive https://github.com/Kingcom/armips.git
  cmake -S armips -B armips/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build armips/build
  install -Dm755 armips/build/armips ~/.local/bin/armips
  ```
* **MIPS toolchain:** any MIPS cross GCC with its binutils; set `MIPS_CC` and `MIPS_BIN` as on macOS. Not yet tested on Arch for this branch.

## License

The code in this repository is released under the [Apache License 2.0](LICENSE); see [NOTICE](NOTICE) for third-party material.

Super Mario 64 is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo.
