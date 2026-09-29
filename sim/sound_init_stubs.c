/* The simulator without SIM_AUDIO=1 has no sound. src/game/sound_init.c is
 * excluded from the link; these no-op stubs cover its game-facing API so the
 * engine's per-frame audio_game_loop_tick() and the music/SFX calls scattered
 * through src/game / src/menu do nothing (and never touch the uninitialised
 * audio state -- audio_init() is only reached from thread4_sound, which is
 * also stubbed here and skipped by the HAL).
 *
 * play_sound()/play_sound_with_freq_scale() etc. (src/audio/external.c) are
 * still linked; they only append to the sSoundRequests ring, which is never
 * drained here -- harmless.
 */
#include <PR/ultratypes.h>
#include "macros.h"

void reset_volume(void) { }
void raise_background_noise(s32 a) { (void) a; }
void lower_background_noise(s32 a) { (void) a; }
void disable_background_sound(void) { }
void enable_background_sound(void) { }
void set_sound_mode(u16 soundMode) { (void) soundMode; }
void play_menu_sounds(s16 soundMenuFlags) { (void) soundMenuFlags; }
void play_menu_sounds_extra(s32 a, void *b) { (void) a; (void) b; }
void play_painting_eject_sound(void) { }
void play_infinite_stairs_music(void) { }
void set_background_music(u16 a, u16 seqArgs, s16 fadeTimer) { (void) a; (void) seqArgs; (void) fadeTimer; }
void fadeout_music(s16 fadeOutTime) { (void) fadeOutTime; }
void fadeout_level_music(s16 fadeTimer) { (void) fadeTimer; }
void play_cutscene_music(u16 seqArgs) { (void) seqArgs; }
void play_shell_music(void) { }
void stop_shell_music(void) { }
void play_cap_music(u16 seqArgs) { (void) seqArgs; }
void fadeout_cap_music(void) { }
void stop_cap_music(void) { }
void audio_game_loop_tick(void) { }
void thread4_sound(UNUSED void *arg) { (void) arg; }
