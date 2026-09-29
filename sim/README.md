# Host simulator

The whole game on a Linux workstation: the real SM64 engine (the decomp,
built with the host gcc) and the port's HAL, rendering through the same code
the Pocket runs, into PPM frames. No FPGA, no Pocket.

```
Gfx (the engine's F3DEX2 master display list)
  -> f3d/f3d_emit.c              F3DEX2 -> GDL (game CPU)
  -> MIRLO lang/c/geom           transform, lighting, clipping, triangle setup (geometry core)
  -> MIRLO lang/c/mrdp/mrdp.c    MRDP's bit-exact C model, into a simulated SDRAM
  -> PPM
```

The graphics task is taken where `hal/sptask_dualcore.c` takes it on the
Pocket (`osSpTaskStartGo`). What the simulator draws is what MRDP draws: the
model is bit-exact with the RTL (MIRLO's `sim/mrdp`), and the geometry
pipeline is the same C the geometry core runs (MIRLO's `sim/geom_full`
checks that on the RTL).

## Build

Needs `decomp/` with its assets extracted (`make assets` in the top level) and
`MIRLO/` checked out.

```
./build.sh                  # -> build/sim        the game: logo, title, file select, ...
SIM_BOOT=demo ./build.sh    # -> build_demo/sim   the attract demos, one after another
SIM_GODDARD=1 ./build.sh    # with the title screen's Mario head
SIM_AUDIO=1 ./build.sh      # with the sound (first: BITWIDTH=64 ../tools/build_sound_le.sh)
```

## Run

```
SIM_DUMP_FRAME=300 SIM_OUT=f300.ppm build/sim            # frame 300, then exit
SIM_DUMP_EVERY=50 SIM_DUMP_PREFIX=f SIM_DUMP_FRAME=1500 build/sim
SIM_PAD="150-153:8000,300-303:10" build/sim             # Start at frame 150, A at 300
```

`sim_main.c` lists every variable. A few that help when looking at one thing:

- `SIM_FIRST_DEMO=n SIM_ACT=a SIM_WARP="frame:x,y,z,yaw"` (demo builds): load
  demo n's level in act a, and at that frame put Mario at (x,y,z) with the
  controller released -- e.g. Whomp's Fortress' Bullet Bill:
  `SIM_FIRST_DEMO=1 SIM_ACT=2 SIM_WARP="60:1280,3700,-100,0"`.
- `SIM_MRDP_PIXEL=x,y,frame` with `SIM_EXTRA_DEFS=-DMRDP_DEBUG_PIXEL`: every
  write to that pixel in that frame, with the command that made it.
- `SIM_MRDP_CAPTURE=file`: the frame's MRDP commands, to replay on the RTL
  (MIRLO's `sim/mrdp`).
- `SIM_DUMP_GDL_RAW=file SIM_DLC_OFF=1`: the frame's GDL, to walk on the
  geometry core's RTL (MIRLO's `sim/geom_full`).
- `SIM_AUDIO_WAV=file.wav` (audio builds): record the sound.
