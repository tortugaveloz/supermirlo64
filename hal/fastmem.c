/* memcpy / memset for the game CPU. picolibc's are its size-optimised
 * byte-at-a-time loops (memcpy-asm.S): with this program's struct copies and
 * f3d_emit's vertex copies that was ~21% of the game CPU's time in a level.
 * Defining them here keeps libc's out of the link. */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = d; const uint8_t *sp = s;
    if ((((uintptr_t)dp ^ (uintptr_t)sp) & 3u) == 0u) {
        while (n && ((uintptr_t)dp & 3u)) { *dp++ = *sp++; n--; }
        uint32_t *dw = (uint32_t *)dp; const uint32_t *sw = (const uint32_t *)sp;
        for (; n >= 16u; n -= 16u, dw += 4, sw += 4) {
            uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
            dw[0] = a; dw[1] = b; dw[2] = c; dw[3] = e;
        }
        for (; n >= 4u; n -= 4u) *dw++ = *sw++;
        dp = (uint8_t *)dw; sp = (const uint8_t *)sw;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *dp = d;
    while (n && ((uintptr_t)dp & 3u)) { *dp++ = (uint8_t)c; n--; }
    uint32_t v = (uint8_t)c * 0x01010101u;
    uint32_t *dw = (uint32_t *)dp;
    for (; n >= 16u; n -= 16u, dw += 4) { dw[0] = v; dw[1] = v; dw[2] = v; dw[3] = v; }
    for (; n >= 4u; n -= 4u) *dw++ = v;
    dp = (uint8_t *)dw;
    while (n--) *dp++ = (uint8_t)c;
    return d;
}
