/* Host-simulator fake for litex/build/.../generated/csr.h.
 * Only the accessors the reused Pocket HAL (.c) files touch are provided.
 * Semantics chosen so the synchronous superloop HAL makes progress:
 *   - vblank "triggered" every poll   -> osRecvMesg blocking path always drains
 *   - apf_bridge status == 0 (idle)   -> EEPROM read/write returns at once
 *   - controller CSRs read 0 (neutral)-> demo input (run_demo_inputs) drives it
 *   - timer0 uptime = monotonic host nanoseconds
 */
#ifndef SIM_FAKE_CSR_H
#define SIM_FAKE_CSR_H
#include <stdint.h>
#include <time.h>

static inline uint64_t _sim_now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t _sim_timer_latched;
static inline void     timer0_uptime_latch_write(uint32_t v) { (void)v; _sim_timer_latched = _sim_now_ns(); }
static inline uint64_t timer0_uptime_cycles_read(void)       { return _sim_timer_latched; }

static inline uint32_t apf_video_video_vblank_triggered_read(void) { return 1u; }
/* a new frame on every read, like the flag above (vi_pump counts frames) */
static inline uint32_t apf_video_video_frame_counter_read(void) { static uint32_t n; return ++n; }

static inline void apf_bridge_slot_id_write(uint32_t v)          { (void)v; }
static inline void apf_bridge_data_offset_write(uint32_t v)      { (void)v; }
static inline void apf_bridge_transfer_length_write(uint32_t v)  { (void)v; }
static inline void apf_bridge_ram_data_address_write(uint32_t v) { (void)v; }
static inline void apf_bridge_request_read_write(uint32_t v)     { (void)v; }
static inline void apf_bridge_request_write_write(uint32_t v)    { (void)v; }
static inline uint32_t apf_bridge_status_read(void)              { return 0u; }

/* sim_main.c's SIM_PAD script (weak: the Fase harnesses do not define it) */
uint32_t sim_pad_key(void) __attribute__((weak));
static inline uint32_t apf_input_cont1_key_read(void) { return sim_pad_key ? sim_pad_key() : 0u; }
static inline uint32_t apf_input_cont1_joy_read(void) { return 0u; }
static inline uint32_t apf_input_cont2_key_read(void) { return 0u; }
static inline uint32_t apf_input_cont2_joy_read(void) { return 0u; }
static inline uint32_t apf_input_cont3_key_read(void) { return 0u; }
static inline uint32_t apf_input_cont3_joy_read(void) { return 0u; }
static inline uint32_t apf_input_cont4_key_read(void) { return 0u; }
static inline uint32_t apf_input_cont4_joy_read(void) { return 0u; }
/* interact.json settings (hal/controller.c's button map, the resolution):
 * SIM_INTERACT="r0,r1,r2,r3" in hex, 0 (= the firmware's defaults) otherwise */
uint32_t sim_interact(int i);
static inline uint32_t apf_interact_interact0_read(void) { return sim_interact(0); }
static inline uint32_t apf_interact_interact1_read(void) { return sim_interact(1); }
static inline uint32_t apf_interact_interact2_read(void) { return sim_interact(2); }
static inline uint32_t apf_interact_interact3_read(void) { return sim_interact(3); }

#endif
