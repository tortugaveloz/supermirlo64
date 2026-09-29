# Super Mirlo 64

Super Mario 64 on [Mirlo](https://github.com/tortugaveloz/MIRLO), a RISC-V computer for the Analogue Pocket with an N64-style rasterizer.

It is a port of the [n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation. The engine is compiled unchanged; this repository adds:
* a libultra layer (`hal/`);
* a translator from the game's F3DEX2 display lists to Mirlo's display lists (`f3d/`);
* the N64 audio microcode as the audio core's firmware (`audio/`).

The game CPU runs the game, Mirlo's geometry core does what the N64's RSP did (transform, lighting, clipping, triangle setup), and MRDP draws. It runs at the N64's own 320 × 240.

**This repository contains none of the game's code or assets.** You need your own copy of the game (the US ROM), from which the build extracts the assets. The game file the build produces contains data from that ROM: do not share it.

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

Tested on Ubuntu 24.04 (x86-64).

### 1. Tools

```bash
sudo apt install build-essential git python3 python3-venv pkgconf
```

**A RISC-V GCC with multilib** (`rv32imafc`/`ilp32f` and `rv32im`/`ilp32`) on your `PATH`, named `riscv-none-elf-*`. The xPack build works:

```bash
mkdir -p ~/opt && cd ~/opt
wget https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz
tar xf xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz
export PATH=~/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
```

**Python packages** for LiteX (MIRLO's build):

```bash
python3 -m venv ~/.venvs/mirlo && source ~/.venvs/mirlo/bin/activate
pip install meson ninja pyserial
```

### 2. Sources

```bash
git clone --recursive https://github.com/tortugaveloz/supermirlo64.git
cd supermirlo64
MIRLO/litex/vendor/setup.sh      # LiteX's submodules + Mirlo's patches
```

### 3. Mirlo

This generates the register headers and the LiteX libraries, and builds the geometry core's firmware:

```bash
(cd MIRLO/litex && make)
(cd MIRLO/lang/c/geom && make)
(cd MIRLO/litex && make)
```

The Pocket needs the Mirlo core, whose bitstream you build with Quartus (see `MIRLO/README.md`). The geometry core's firmware is part of that bitstream, and the game reads one of its variables. So build the game against the same `MIRLO/lang/c/geom/build/geom.elf` the bitstream was built with, or pass `GEOM_ELF=<that geom.elf>` to `make` below.

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

`make assets` runs the decomp's own asset extraction and generators. It ends with errors from its MIPS build, as expected: only the generated sources are used.

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
| Start (+) | Start |
| Select (−) | C-down |

The core settings (Pocket menu → Core Settings) choose:
* the stick and N64 D-pad sources;
* whether Start needs Select+Start;
* whether R works as a modifier, turning X/B/Y/A into the four C buttons.

The Pocket's own Controls menu remaps the physical buttons.

## The host simulator

`sim/` builds the whole game for Linux with the host compiler and renders every frame through the same translator, geometry pipeline and MRDP model the Pocket runs. It needs step 5's `make assets`, not a Pocket or a bitstream. See `sim/README.md`.

```bash
sim/build.sh
SIM_PAD="150-153:8000,300-303:10" SIM_DUMP_EVERY=50 SIM_DUMP_FRAME=1000 sim/build/sim
```

## License

The code in this repository is released under the [Apache License 2.0](LICENSE); see [NOTICE](NOTICE) for third-party material.

Super Mario 64 is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo.
