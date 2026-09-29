#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include <stdint.h>

/* Load the sound data and the audio core's firmware, then audio_init() +
 * sound_init(). Falls back to a silent game if either is missing. */
void audio_hal_init(void);
/* Start the 60 Hz audio tick (Pocket: machine-timer interrupt). */
void audio_hal_start(void);
/* One vblank of the sound thread: sequence player + command list + task. */
void audio_hal_tick(void);

struct audio_hal_stats {
    uint32_t ticks, ticks_late;
    uint32_t tasks, tasks_dropped, last_cmds;
    uint32_t ai_buffers, ai_dropped;
    uint32_t cpu_cycles, cpu_cycles_max;     /* game CPU per tick */
    uint32_t core_cycles, core_cycles_max;   /* audio core per task */
};
extern struct audio_hal_stats g_audio_stats;

#endif
