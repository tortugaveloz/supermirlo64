/* The simulator without SIM_AUDIO=1 has no sound: the sound data build
 * (tools/build_sound_le.sh) is left out. src/audio/load.c still references these four
 * blobs from audio_reset_session(); the sound thread that would touch them is
 * not started by the HAL (hal/os_thread_hal.c). Zero-filled so any stray
 * header read during init sees an empty table rather than an OOB fault.
 */
#include <PR/ultratypes.h>

u8 gSoundDataADSR[512];  /* sound_data.ctl */
u8 gSoundDataRaw[512];   /* sound_data.tbl */
u8 gMusicData[512];      /* sequences.s   */
u8 gBankSetsData[512];   /* bank_sets.s   */
