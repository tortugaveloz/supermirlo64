/* N64 audio microcode (ABI, the pre-EU "old" command set SM64 US emits),
 * interpreted in integer C. Runs on the audio core; the same file builds on
 * the host (the simulator's SIM_AUDIO=1). */
#ifndef AUDIO_ABI_H
#define AUDIO_ABI_H

#include <stdint.h>

/* Opcodes and flags: include/PR/abi.h, non-SH/CN block. */
#define ABI_SPNOOP     0
#define ABI_ADPCM      1
#define ABI_CLEARBUFF  2
#define ABI_ENVMIXER   3
#define ABI_LOADBUFF   4
#define ABI_RESAMPLE   5
#define ABI_SAVEBUFF   6
#define ABI_SEGMENT    7
#define ABI_SETBUFF    8
#define ABI_SETVOL     9
#define ABI_DMEMMOVE   10
#define ABI_LOADADPCM  11
#define ABI_MIXER      12
#define ABI_INTERLEAVE 13
#define ABI_POLEF      14
#define ABI_SETLOOP    15

#define ABI_F_INIT 0x01
#define ABI_F_LOOP 0x02
#define ABI_F_LEFT 0x02
#define ABI_F_VOL  0x04
#define ABI_F_AUX  0x08

/* Execute one command. w1 carries a DRAM pointer for the commands that take
 * one (hence uintptr_t: 64-bit on the host test build). */
void abi_exec(uint32_t w0, uintptr_t w1);

/* Reset all interpreter state (between sessions/tests). */
void abi_reset(void);
/* Writes the per-note states held in DMEM back to DRAM (ABI_STATE_CACHE). */
void abi_state_flush(void);

/* Count of commands by opcode since the last abi_reset (diagnostics). */
extern uint32_t abi_op_count[16];

#endif
