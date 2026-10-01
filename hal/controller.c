#include <generated/csr.h>

#include "controller.h"

// APF_INPUT bit positions (MIRLO's docs/control.md: Controller/Joystick Bitmap).
#define KEY_DPAD_UP (1u << 0)
#define KEY_DPAD_DOWN (1u << 1)
#define KEY_DPAD_LEFT (1u << 2)
#define KEY_DPAD_RIGHT (1u << 3)
#define KEY_SELECT (1u << 14)
#define KEY_START (1u << 15)
#define KEY_TYPE(k) ((k) >> 28)     // 1 Pocket, 2 dock pad without analog, 3 dock pad with analog

// Full deflection on the N64's documented -80..80 stick range.
#define DPAD_STICK 80

/* ---- button mapping -----------------------------------------------------
 * Fixed: the Pocket's own Controls menu (input.json) remaps which physical
 * button sends each key, so the core settings no longer duplicate it. Every
 * N64 input follows one APF_INPUT key (field = 1 + the key's bit: 1..4 D-pad
 * up/down/left/right, 5 A, 6 B, 7 X, 8 Y, 9 L1, 10 R1, 15 select, 16 start):
 * A/B, Z on L1, Start on "+", R on R1, C-left on Y, C-right on X, C-down on
 * Select ("-"); C-up unmapped. Under "Start = Select+Start" the Start key
 * alone does nothing.
 * The core settings left (interact.json, apf_interact slots 0/1, bridge
 * 0x1000_0100/0x1000_0104):
 *   slot 0 [14:10] "R = modifier" (id 212, 0x10000110, the old Z field): 1 on
 *          [24:20] N64 L (id 213, 0x10000118): 0 none, else 1 + an APF_INPUT
 *                  key bit (11 L2, 12 R2, 13 L3, 14 R3)
 *   slot 1 [21:20] stick source (0 D-pad, 1 left analog stick, 2 none),
 *          [23:22] N64 D-pad source (0 none, 1 Pocket D-pad, 2 right stick),
 *          (the analog sources only on a pad the dock reports as analog, type
 *          3: without one "L stick" falls back to the D-pad, "R stick" to none)
 *          [24] Show FPS, [25] Start = Select+Start */
#define MAP0 (5u << 0 | 6u << 5 | 9u << 10 | 16u << 15 | 0u << 20 | 10u << 25)   /* A B Z Start L R */
#define MAP1 (0u << 0 | 15u << 5 | 8u << 10 | 7u << 15)                          /* C-up/down/left/right */
#define KEY_A  (1u << 4)
#define KEY_B  (1u << 5)
#define KEY_X  (1u << 6)
#define KEY_Y  (1u << 7)
#define KEY_R1 (1u << 9)

static inline int mapped(uint32_t key, uint32_t reg, int field)
{
    uint32_t f = (reg >> (5 * field)) & 31u;
    return f != 0 && ((key >> (f - 1)) & 1u);
}

/* An analog axis (0..255, 0x80 centre) on the N64's -80..80, with a small
 * dead zone around the centre. The D-pad stays the default: with some pads
 * the dock does not deliver the documented layout. Measured 2026-09-30 on a
 * type-3 pad: the Y bytes were clean 8-bit axes, but each X byte idled at
 * about +80 with +-15 of noise and wrapped around while the stick moved
 * straight up or down -- the low byte of a wider axis, not its top 8 bits. */
static int8_t axis(uint32_t raw, int up)
{
    int v = (int)(raw & 0xFFu) - 128;
    if (up) v = -v;                      /* screen-down positive -> N64 up positive */
    if (v > -12 && v < 12) return 0;
    v = v * 80 / 127;
    return (int8_t)(v > 80 ? 80 : v < -80 ? -80 : v);
}

void hal_read_controller(OSContPad *pad, int index)
{
    uint32_t key, joy;
    switch (index) {
    case 0: key = apf_input_cont1_key_read(); joy = apf_input_cont1_joy_read(); break;
    case 1: key = apf_input_cont2_key_read(); joy = apf_input_cont2_joy_read(); break;
    case 2: key = apf_input_cont3_key_read(); joy = apf_input_cont3_joy_read(); break;
    default: key = apf_input_cont4_key_read(); joy = apf_input_cont4_joy_read(); break;
    }
    uint32_t set0 = apf_interact_interact0_read(), set1 = apf_interact_interact1_read();
    const uint32_t m0 = MAP0, m1 = MAP1;

    uint16_t button = 0;
    /* "R = modifier": R1 held turns the face buttons into the C buttons where
     * they sit (X up, B down, Y left, A right) and the D-pad into half a
     * stick; R itself never reaches the game then */
    int rmod_on = ((set0 >> 10) & 31u) == 1u;
    int rmod = rmod_on && (key & KEY_R1);
    if (rmod_on) key &= ~KEY_R1;
    if (rmod) {
        if (key & KEY_X) button |= CONT_E;      // C-up
        if (key & KEY_B) button |= CONT_D;      // C-down
        if (key & KEY_Y) button |= CONT_C;      // C-left
        if (key & KEY_A) button |= CONT_F;      // C-right
        key &= ~(KEY_A | KEY_B | KEY_X | KEY_Y);
    }
    /* "Start = Select+Start" (interact.json id 103, slot 1 bit 25): the N64
     * Start is Select and Start held together -- and then neither counts for
     * anything else; Start alone does nothing. */
    if ((set1 >> 25) & 1u) {
        const uint32_t chord = KEY_SELECT | KEY_START;
        if ((key & chord) == chord) {
            button |= CONT_START;
            key &= ~chord;
        }
    } else if (mapped(key, m0, 3)) {
        button |= CONT_START;
    }
    if (mapped(key, m0, 0)) button |= CONT_A;
    if (mapped(key, m0, 1)) button |= CONT_B;
    if (mapped(key, m0, 2)) button |= CONT_G;      // Z
    if (mapped(key, set0, 4)) button |= CONT_L;    // a core setting, not in MAP0
    if (mapped(key, m0, 5)) button |= CONT_R;
    if (mapped(key, m1, 0)) button |= CONT_E;      // C-up
    if (mapped(key, m1, 1)) button |= CONT_D;      // C-down
    if (mapped(key, m1, 2)) button |= CONT_C;      // C-left
    if (mapped(key, m1, 3)) button |= CONT_F;      // C-right

    const int analog = KEY_TYPE(key) == 3u;
    uint32_t stick = (set1 >> 20) & 3u;
    if (stick == 1u && !analog) stick = 0;      // "L stick" on a pad without one: the D-pad

    int8_t sx = 0, sy = 0;
    switch (stick) {
    case 0: {   // the D-pad, at full deflection (diagonals too: SM64 clamps the magnitude) -- half under the R modifier
        int8_t d = rmod ? DPAD_STICK / 2 : DPAD_STICK;
        if (key & KEY_DPAD_LEFT) sx -= d;
        if (key & KEY_DPAD_RIGHT) sx += d;
        if (key & KEY_DPAD_UP) sy += d;
        if (key & KEY_DPAD_DOWN) sy -= d;
        /* half a stick is below SM64's clamp: a diagonal (40, 40) was 1.41x
         * the straight 40 -- scaled by 1/sqrt(2) to the same magnitude */
        if (rmod && sx && sy) {
            sx = (int8_t)(sx < 0 ? -28 : 28);
            sy = (int8_t)(sy < 0 ? -28 : 28);
        }
        break;
    }
    case 1:     // the dock controller's left stick
        sx = axis(joy, 0);
        sy = axis(joy >> 8, 1);
        break;
    default:
        break;
    }
    switch ((set1 >> 22) & 3u) {
    case 1:
        if (key & KEY_DPAD_UP) button |= CONT_UP;
        if (key & KEY_DPAD_DOWN) button |= CONT_DOWN;
        if (key & KEY_DPAD_LEFT) button |= CONT_LEFT;
        if (key & KEY_DPAD_RIGHT) button |= CONT_RIGHT;
        break;
    case 2: {   // the right stick, as four digital directions
        if (!analog) break;
        int rx = (int)((joy >> 16) & 0xFFu) - 128, ry = (int)((joy >> 24) & 0xFFu) - 128;
        if (rx < -64) button |= CONT_LEFT;
        if (rx > 64) button |= CONT_RIGHT;
        if (ry < -64) button |= CONT_UP;
        if (ry > 64) button |= CONT_DOWN;
        break;
    }
    default:
        break;
    }

    pad->button = button;
    pad->stick_x = sx;
    pad->stick_y = sy;
    pad->errnum = 0;
}
