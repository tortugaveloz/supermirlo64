/* See mirlo_game.h. */
#include <string.h>
#include <generated/csr.h>
#include <system.h>
#include "mirlo_game.h"

#define FOOTER_BYTES 32u
#define MAX_BLOCKS   16u

static uint32_t s_buf[(FOOTER_BYTES + MAX_BLOCKS * 16u) / 4u] __attribute__((aligned(64)));

static int slot_read(uint32_t slot, uint32_t off, uint32_t len, uint32_t addr)
{
    apf_bridge_slot_id_write(slot);
    apf_bridge_data_offset_write(off);
    apf_bridge_transfer_length_write(len);
    apf_bridge_ram_data_address_write(addr);
    apf_bridge_request_read_write(1);
    for (uint32_t n = 0; apf_bridge_status_read() != 1; )
        if (++n == 200000000u) return 0;
    flush_cpu_dcache();                       /* the bridge wrote SDRAM behind the D-cache */
    return 1;
}

int mirlo_read(uint32_t offset, uint32_t len, uint32_t addr)
{
    return slot_read(0, offset, len, addr);
}

int mirlo_find_block(const char *tag, uint32_t *offset, uint32_t *size)
{
    apf_bridge_slot_id_write(0);
    uint32_t fsz = apf_bridge_file_size_read();
    if (fsz <= FOOTER_BYTES || !slot_read(0, fsz - FOOTER_BYTES, FOOTER_BYTES, (uint32_t)(uintptr_t)s_buf))
        return 0;
    const volatile uint32_t *f = s_buf;
    if (memcmp((const void *)&s_buf[4], "MIRLODATA", 10) != 0 || f[3] != 1u) return 0;
    uint32_t n = f[1], table = f[2];
    if (n == 0u || n > MAX_BLOCKS || table + n * 16u > fsz - FOOTER_BYTES) return 0;
    if (!slot_read(0, table, n * 16u, (uint32_t)(uintptr_t)s_buf)) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const volatile uint32_t *e = &s_buf[i * 4u];
        if (memcmp((const void *)&s_buf[i * 4u], tag, 4) == 0 && e[1] + e[2] <= fsz) {
            *offset = e[1]; *size = e[2];
            return 1;
        }
    }
    return 0;
}

unsigned mirlo_video_hres(void)
{
    apf_bridge_slot_id_write(0);
    uint32_t fsz = apf_bridge_file_size_read();
    if (fsz <= FOOTER_BYTES || !slot_read(0, fsz - FOOTER_BYTES, FOOTER_BYTES, (uint32_t)(uintptr_t)s_buf))
        return 268u;
    const volatile uint8_t *f = (const volatile uint8_t *)s_buf;
    if (memcmp((const void *)&s_buf[4], "MIRLODATA", 10) != 0 || s_buf[3] != 1u) return 268u;
    return f[26] == 1u ? 320u : 268u;
}
