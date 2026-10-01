#ifndef HAL_CONTROLLER_H
#define HAL_CONTROLLER_H

#include <ultra64.h>

// Polls APF_INPUT (MIRLO's docs/control.md; no interrupts on this
// SoC -- must be polled every frame) for controller `index` (0-3, matching
// CONT1-4) and fills `pad` in libultra's OSContPad format, the same struct
// src/game/game_init.c's read_controller_inputs()/gControllerPads[] expect.
// Mapping: fixed (the Pocket's Controls menu remaps the physical buttons):
// D-pad -> control stick, A/B -> A/B, X/Y -> C-right/left, L1 -> Z, R1 -> R,
// "+" -> Start, "-" -> C-down. Core settings: the stick and N64 D-pad
// sources, the N64 L button (a Dock pad's L2/R2/L3/R3), "Start =
// Select+Start" and "R = modifier" (R1 held: X/B/Y/A are C-up/down/left/right, the D-pad half a
// stick; no N64 R). controller.c has the register layout.
void hal_read_controller(OSContPad *pad, int index);

#endif // HAL_CONTROLLER_H
