/* The audio firmware's (abi_main.c) mailbox protocol: the N64's RSP audio
 * task + AI, over the audio core's eight mailbox words (MIRLO's
 * lang/c/audio/audio_hw.h: AUDIO_MBOX, the core's other states). Used by
 * both sides: abi_main.c on the audio core, hal/audio_hal.c on the game CPU. */
#ifndef ABI_MAILBOX_H
#define ABI_MAILBOX_H

/*
 *   MBOX0 STATE     core: AUDIO_ST_ABI when idle, AUDIO_ST_ABI_BUSY while a
 *                   task runs (AUDIO_ST_TRAP on a trap)
 *   MBOX1 TASK      game: command-list address; non-zero = a task is queued.
 *                   core: clears it to 0 when it TAKES the task (starts it),
 *                   so the game can queue the next one while this one runs:
 *                   a queue of two. A task is done when MBOX1 is 0 and MBOX0
 *                   is AUDIO_ST_ABI -- or when the next one has been taken.
 *   MBOX2 TASK_N    game: command count, written BEFORE MBOX1
 *   MBOX3 AI_NEXT   game: PCM buffer address queued as the AI's "next"
 *                   buffer (osAiSetNextBuffer); non-zero = slot taken.
 *                   core: clears it when that buffer becomes the current one.
 *   MBOX4 AI_LEN    game: its length in bytes, written BEFORE MBOX3
 *   MBOX5 AI_REMAIN core: bytes not yet played of what the AI was handed
 *                   (current buffer + what sits in the APF FIFO), at the
 *                   AI frequency -- what osAiGetLength() returns
 *   MBOX6 AI_FREQ   game: DAC rate in Hz (osAiSetFrequency); 0 = 32000
 *   MBOX7 STATS     core: cycles the last task took
 * The same two-slot AI model as the N64 (current + next): osAiSetNextBuffer
 * fails while MBOX3 is non-zero. */
#define AUDIO_ST_ABI      0xA0D1AB10u
#define AUDIO_ST_ABI_BUSY 0xA0D1AB11u
#define AMB_STATE     0
#define AMB_TASK      1
#define AMB_TASK_N    2
#define AMB_AI_NEXT   3
#define AMB_AI_LEN    4
#define AMB_AI_REMAIN 5
#define AMB_AI_FREQ   6
#define AMB_STATS     7

#endif
