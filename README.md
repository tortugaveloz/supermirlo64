# Super Mirlo 64

Super Mario 64 on [Mirlo](https://github.com/tortugaveloz/MIRLO), a RISC-V computer for the Analogue Pocket inspired by the N64 hardware.

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
sudo apt install build-essential git python3 python3-venv pkgconf
```

**A RISC-V GCC with multilib** (`rv32imafc`/`ilp32f` and `rv32im`/`ilp32`) on your `PATH`, named `riscv-none-elf-*`. The xPack build works. Unpack it wherever you like (the commands below use the current directory) and, from that directory, add it to your `PATH` (repeat the `export` in every new shell):

```bash
wget https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz
tar xf xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz
export PATH="$PWD/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
```

**Python 3.10 or later.** Tested with 3.12 and 3.14. (LiteX/Migen infer object names from variable names by reading bytecode; `MIRLO/litex/vendor/setup.sh` patches Migen so that this works on current Python versions.)

**Python packages** for LiteX (MIRLO's build):

```bash
python3 -m venv ~/.venvs/mirlo && source ~/.venvs/mirlo/bin/activate
pip install meson ninja pyserial packaging
```

### 2. Sources

```bash
git clone --recursive https://github.com/tortugaveloz/supermirlo64.git
cd supermirlo64
git submodule update --init --recursive    # in case the clone left submodules unchecked-out
MIRLO/litex/vendor/setup.sh                # LiteX's submodules + Mirlo's patches
```

### 3. Mirlo

This generates the register headers and the LiteX libraries, and builds the geometry core's firmware:

```bash
(cd MIRLO/litex && make)
(cd MIRLO/lang/c/geom && make)
(cd MIRLO/litex && make)
```

The Pocket needs the Mirlo core, whose bitstream you build with Quartus (see `MIRLO/README.md`). The geometry core's firmware is part of that bitstream, and the game reads one of its variables. So build the game against the same `MIRLO/lang/c/geom/build/geom.elf` the bitstream was built with, or pass `GEOM_ELF=<that geom.elf>` to `make` below.

The released Mirlo core's firmware was built with the xPack `riscv-none-elf-gcc` 15.2.0 recommended above. With that toolchain the variable is at address `0x20008030`, which you can check:

```bash
riscv-none-elf-nm MIRLO/lang/c/geom/build/geom.elf | grep geom_tris_emitted    # 20008030 B geom_tris_emitted
```

If yours differs (another compiler version, or a different Mirlo revision), the game would read the wrong address: use the `geom.elf` your bitstream was built from, via `GEOM_ELF=`.

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
* The stick and N64 D-pad sources can also be configured.

## Building on macOS

Tested on macOS 26, Apple silicon, with Homebrew. The build is the same as above, with these differences:

```bash
xcode-select --install                 # clang, git, make 3.81 (too old, see below)
brew install make python@3.12          # gmake 4.x; Python
```

* **GNU Make:** Apple ships make 3.81, which cannot run the decomp's Makefile (it uses `!=`, from 4.0). Use `gmake` instead of `make` in every step, or put Homebrew's `gnubin` first on your `PATH`: `export PATH="$(brew --prefix make)/libexec/gnubin:$PATH"`. The top-level Makefile stops with a message if it is run by make 3.81.
* **RISC-V GCC:** the xPack build for macOS, `darwin-arm64` (Apple silicon) or `darwin-x64` (Intel):

  Unpack it wherever you like and, from that directory:

  ```bash
  curl -LO https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-darwin-arm64.tar.gz
  tar xf xpack-riscv-none-elf-gcc-15.2.0-1-darwin-arm64.tar.gz
  export PATH="$PWD/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
  ```

  If macOS refuses to run the binaries because they were downloaded, `xattr -dr com.apple.quarantine xpack-riscv-none-elf-gcc-15.2.0-1`.
* **Python:** `python3.12 -m venv ~/.venvs/mirlo`, then the `pip install` from step 1. Newer versions work too.
* **ROM checksum:** `shasum baserom.us.z64` instead of `sha1sum`.
* **Job count:** the Makefile finds it with `sysctl` when `nproc` does not exist; use `-j$(sysctl -n hw.ncpu)` for the main build.

The whole sequence:

```bash
export PATH="$(brew --prefix make)/libexec/gnubin:$PATH"
export PATH="/path/to/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"   # where you unpacked it
source ~/.venvs/mirlo/bin/activate
git submodule update --init --recursive
MIRLO/litex/vendor/setup.sh
(cd MIRLO/litex && make) && (cd MIRLO/lang/c/geom && make) && (cd MIRLO/litex && make)
cp /path/to/your.z64 baserom.us.z64
make assets && make -j$(sysctl -n hw.ncpu)
```

The bundled `armips` and the decomp's other host tools build with Apple's clang. The `ido-static-recomp` error during `make assets` is part of the expected MIPS failure.

## Building on Arch Linux

Arch, CachyOS and similar distributions ship newer GCC and Python than Ubuntu 24.04:

```bash
sudo pacman -S --needed base-devel git python cmake ninja pkgconf
```

* **Python:** the system Python works (tested up to 3.14) once `setup.sh` has patched Migen (step 2). Make sure to run `setup.sh`, and install `packaging` in the venv.
* **armips:** if the bundled `decomp/tools/armips.cpp` fails to compile with your GCC (errors such as `exponent has no digits` or missing `uint8_t`/`INT64_C`), build a current [Kingcom/armips](https://github.com/Kingcom/armips) (0.11 or later; there is no Arch package) and put it on your `PATH`. The decomp's Makefile uses a system `armips` when it finds one:

  ```bash
  git clone --recursive https://github.com/Kingcom/armips.git
  cmake -S armips -B armips/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build armips/build
  install -Dm755 armips/build/armips ~/.local/bin/armips
  ```
* **RISC-V toolchain:** `riscv-none-elf-*` on your `PATH`, as above. `MIRLO/lang/c/*/Makefile` use `CROSS ?= riscv-none-elf-`; if yours is elsewhere, pass `CROSS=/path/to/riscv-none-elf-`.

## License

The code in this repository is released under the [Apache License 2.0](LICENSE); see [NOTICE](NOTICE) for third-party material.

Super Mario 64 is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo.
