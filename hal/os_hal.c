// libultra's leaf functions the game and engine call directly: timing, cache
// maintenance, address translation, PI "DMA" and the EEPROM save. Threads,
// message queues and the controller are in os_thread_hal.c.

#include <string.h>
#include "log.h"          /* MIRLO lang/c/game: queued, frame-loop safe */

#include <ultra64.h>

#include <generated/csr.h>

// ---- Timers -----------------------------------------------------------------
// TIMER0's uptime: a free-running 64-bit count of system clock cycles,
// read coherently via the latch (MIRLO's docs/control.md). (On the RISC-V
// Mirlo the CPU's own `cycle` CSR was not implemented -- reading it trapped.)
static uint64_t rdcycle64(void)
{
    timer0_uptime_latch_write(1);
    return timer0_uptime_cycles_read();
}

OSTime osGetTime(void)
{
    return (OSTime)rdcycle64();
}

void osSetTime(OSTime time)
{
    (void)time; // No settable RTC path wired up yet; not needed for gameplay.
}

// ---- Cache maintenance --------------------------------------------------------
// No-ops: every buffer another master reads or writes is flushed where it is
// handed over (frame.c for the GDL, hal/audio_hal.c for the audio core,
// hal/mirlo_game.c for the bridge).
void osInvalDCache(void *addr, size_t size) { (void)addr; (void)size; }
void osInvalICache(void *addr, size_t size) { (void)addr; (void)size; }
void osWritebackDCache(void *addr, size_t size) { (void)addr; (void)size; }
void osWritebackDCacheAll(void) { }

// ---- Virtual/physical address translation --------------------------------------
// Flat memory model (NO_SEGMENTED_MEMORY): every address is already
// "physical" on this target.
uintptr_t osVirtualToPhysical(void *addr)
{
    return (uintptr_t)addr;
}

// ---- TLB (N64 MMU) -------------------------------------------------------------
// Only reached from a path the game never takes (src/game/main.c) -- no-ops.
void osMapTLB(s32 index, OSPageMask pageMask, void *entryLo0, u32 pfn0, u32 pfn1, s32 asid)
{
    (void)index; (void)pageMask; (void)entryLo0; (void)pfn0; (void)pfn1; (void)asid;
}
void osMapTLBRdb(void) { }
void osUnmapTLBAll(void) { }

// ---- PI ("cartridge") DMA -------------------------------------------------------
// The whole game image is resident in flat SDRAM on this target -- no real
// DMA or latency to hide, so this is just a
// synchronous memcpy. `mq`/`mesg` (the completion message queue) are
// ignored: callers that osRecvMesg-wait for DMA completion need that queue
// to already have a message available by the time they check, which is
// trivially true here since the copy already happened before we return.
s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, uintptr_t devAddr, void *dramAddr, size_t size,
                  OSMesgQueue *mq)
{
    (void)priority;
    if (direction == OS_READ) {
        memcpy(dramAddr, (const void *)(uintptr_t)devAddr, size);
    } else {
        memcpy((void *)(uintptr_t)devAddr, dramAddr, size);
    }
    /* The copy is already done synchronously, but callers (src/game/memory.c
     * dma_read) then osRecvMesg(mq, ..., OS_MESG_BLOCK) -- and mq is not
     * vblank-fed, so the blocking path in os_thread_hal.c would spin forever.
     * Post the completion message the caller is waiting for. */
    if (mq != NULL) {
        osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    }
    return 0;
}

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *msgBuf, s32 msgCount)
{
    (void)pri; (void)cmdQ; (void)msgBuf; (void)msgCount;
}

// ---- EEPROM (save game) ---------------------------------------------------------
// SM64's 4-Kbit EEPROM (512 bytes, EEPROM_SIZE in save_file.c), backed by data
// slot 1 of MIRLO's pkg/pocket/Cores/tortuga.Mirlo/data.json ("Save":
// nonvolatile, NOT deferload, 0x200
// bytes at 0x03FF0000, parameters bit 2 = named after the game + ".sav", bit 5
// = 0xFF-filled when there is no file yet, i.e. an erased EEPROM).
//
// The Pocket itself moves it: it writes the file to SDRAM 0x43FF0000 (bridge
// 0x03FF0000, apf_wishbone_master's absolute range; above the SM64 pool)
// before the core starts, and reads it back into the file when the core
// quits, the Pocket sleeps or powers off. So the game just uses that memory.
// (The core's own data slot read/write of this slot returns result 2, "error
// or out of range": saves never reached the card that way.)
// A save is therefore on the card after quitting, not when the game saves.
//
// save_file.c only checks gEepromProbe != 0 -- but it is an s8, so the value
// must survive the truncation (see osEepromProbe);
// address/nbytes are libultra's 8-byte blocks.
#define EEPROM_BYTES 0x200

s32 osEepromProbe(OSMesgQueue *mq)
{
    (void)mq;
    /* 1 = EEPROM_TYPE_4K, what libultra returns for SM64's 512-byte EEPROM.
     * NOT CONT_EEP16K (0x4000): game_init.c keeps the probe in an s8
     * gEepromProbe, where 0x4000 truncates to 0 -- "no EEPROM" -- and the
     * game then never read nor wrote a save (every file NEW after a
     * relaunch, on the Pocket 2026-09-26). */
    return 1;
}

#if defined(PORT_EEPROM_RAM) || defined(GEOM_HOST_TEST)
/* The host sim: the save lives in RAM only. */
static u8 s_eeprom_ram[EEPROM_BYTES];
#define s_eeprom s_eeprom_ram
static void eeprom_load(void) { memset(s_eeprom, 0xFF, sizeof s_eeprom_ram); }
#else
/* Written by the Pocket before the core started (D-cache still cold); the
 * D-cache is write-through, so the game's writes are in SDRAM for the flush. */
#define s_eeprom ((volatile u8 *)0x43FF0000u)
static void eeprom_load(void) { }
#endif
static int s_eeprom_loaded;

#if !defined(PORT_EEPROM_RAM) && !defined(GEOM_HOST_TEST)
/* The Pocket writes a nonvolatile slot back with the size the core's data
 * slot table gives it (Analogue's data.json docs) -- the size of the file it
 * loaded, 0 when there was none. So with no .sav yet it wrote nothing at
 * quit, and no save ever reached the card (Saves/mirlo/ did not exist,
 * 2026-09-28). The table is set to the EEPROM's 512 bytes here, once, at
 * boot (apf_bridge file_size: a write updates the table entry of slot_id). */
static void bridge_settle(void) { for (volatile int i = 0; i < 256; i++) { } }
uint32_t port_save_slot_init(void)
{
    uint32_t prev = apf_bridge_slot_id_read(), was;
    apf_bridge_slot_id_write(1);            /* data.json slot 1: "Save" */
    bridge_settle();
    was = apf_bridge_file_size_read();
    if (was != EEPROM_BYTES) {
        apf_bridge_file_size_write(EEPROM_BYTES);
        bridge_settle();                    /* the table write lands in clk_74a under this slot_id */
    }
    apf_bridge_slot_id_write(prev);
    return was;
}
#endif

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    if (!s_eeprom_loaded) { eeprom_load(); s_eeprom_loaded = 1; }
    for (int i = 0; i < nbytes; i++) {
        u32 a = (u32)address * 8u + (u32)i;
        buffer[i] = a < EEPROM_BYTES ? s_eeprom[a] : 0xFF;
    }
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    if (!s_eeprom_loaded) { eeprom_load(); s_eeprom_loaded = 1; }
    for (int i = 0; i < nbytes; i++) {
        u32 a = (u32)address * 8u + (u32)i;
        if (a < EEPROM_BYTES) s_eeprom[a] = buffer[i];
    }
#if !defined(PORT_EEPROM_RAM) && !defined(GEOM_HOST_TEST)
    log_printf("EEPROM write @%u x%d\n", (unsigned)address * 8u, nbytes);
#endif
    return 0;
}
