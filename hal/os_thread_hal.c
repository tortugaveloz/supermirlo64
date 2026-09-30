// libultra's threading, message-queue, VI and controller-init surface as a
// *synchronous, single-call-stack* model. The game's threads have no real
// concurrency between them (each blocks on a vblank message and does its
// per-frame work; sound runs from the audio tick, hal/audio_hal.c), so
// osStartThread calls the thread entry function
// directly instead of starting a real thread, so the whole chain of
// osStartThread calls in the real boot sequence (thread1 -> thread3 ->
// thread4 + thread5) ends up as ordinary nested C calls on one stack, with
// thread5_game_loop's infinite per-frame loop at the bottom -- which is
// fine, since nothing above it was doing anything concurrent anyway.
//
// The one piece of real synchronization this still needs: display_and_vsync()
// (game_init.c) blocks via osRecvMesg waiting for vblank messages that on
// real hardware arrive from a VI interrupt handler. There is no interrupt
// here, so osRecvMesg's blocking path actively polls APF_VIDEO's vblank
// CSR itself and self-delivers to any queue registered via
// osSetEventMesg(OS_EVENT_VI, ...)/osViSetEvent -- see vi_pump() below.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include <generated/csr.h>

#include "controller.h"

#ifdef PORT_FULL_GAME
#include "types.h"   /* struct VblankHandler */
/* src/game/main.c globals -- the SM64 vblank-callback list. Real handle_vblank()
 * osSendMesg's to these; the collapsed-thread HAL has no handle_vblank, so
 * vi_pump() delivers to them directly (see below). */
extern struct VblankHandler *gVblankHandler1;
extern struct VblankHandler *gVblankHandler2;
#endif

// ---- osInitialize / misc data symbols --------------------------------------

void osInitialize(void) { }

u64 osClockRate = 46875000ULL * 2; // Plausible placeholder; nothing here reads it meaningfully yet.
u32 osTvType = 1;                  // 1 == NTSC (see os.h's osTvType values) -- matches this SoC's 60Hz timing.
OSViMode osViModeTable[4];         // Zero-initialized; osViSetMode below doesn't read it (see its comment).

// ---- VI (video interface) --------------------------------------------------
// Mirlo's video is set up by MIRLO's frame.c (the game file asks for
// 320 x 240) -- there is no VI to reprogram, so these are no-ops.

void osCreateViManager(OSPri pri) { (void)pri; }
void osViSetMode(OSViMode *mode) { (void)mode; }
void osViBlack(u8 active) { (void)active; }
void osViSetSpecialFeatures(u32 func) { (void)func; }

void osViSwapBuffer(void *frameBufPtr)
{
    // The game's framebuffers are never drawn to: MRDP draws into frame.c's
    // own, and frame.c flips them on vblank.
    (void)frameBufPtr;
}

// ---- Message queues + event delivery ----------------------------------------

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count)
{
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

/* The audio tick runs from a timer interrupt on the Pocket (hal/audio_hal.c)
 * and uses message queues too (osPiStartDma's completion message), so a
 * queue update must not be split by it: mask interrupts around each one. */
#if (defined(__riscv) || defined(__mips__)) && !defined(GEOM_HOST_TEST)
#include "trap_arch.h"      /* irq_save() / irq_restore(), MIRLO's lang/c/game */
#else
static inline unsigned long irq_save(void) { return 0; }
static inline void irq_restore(unsigned long m) { (void)m; }
#endif

static void mq_push_locked(OSMesgQueue *mq, OSMesg m);
static bool mq_pop_locked(OSMesgQueue *mq, OSMesg *out);
static void mq_push(OSMesgQueue *mq, OSMesg m)
{
    unsigned long s = irq_save();
    mq_push_locked(mq, m);
    irq_restore(s);
}
static bool mq_pop(OSMesgQueue *mq, OSMesg *out)
{
    unsigned long s = irq_save();
    bool r = mq_pop_locked(mq, out);
    irq_restore(s);
    return r;
}

static void mq_push_locked(OSMesgQueue *mq, OSMesg m)
{
    if (mq->validCount >= mq->msgCount) {
        return; // Full: drop rather than corrupt the ring (shouldn't happen
                // with this game's actual usage patterns -- small, regularly
                // drained queues).
    }
    s32 tail = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[tail] = m;
    mq->validCount++;
}

static bool mq_pop_locked(OSMesgQueue *mq, OSMesg *out)
{
    if (mq->validCount == 0) {
        return false;
    }
    *out = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return true;
}

// Event registry (osSetEventMesg / osViSetEvent): which queue+message to
// deliver when a given OS_EVENT_* "interrupt" happens. Only OS_EVENT_VI is
// actually pumped (see vi_pump) -- this game doesn't register anything else
// through this path in a way that needs live delivery (SP/DP "task done"
// is delivered directly at the point of completion, see hal/sptask_hal.c
// -- but that file calls mq_push through the wrapper below, not by
// reaching into this file's statics, so event registration isn't needed
// for it).
#define MAX_EVENTS 32
static OSMesgQueue *s_event_queue[MAX_EVENTS];
static OSMesg s_event_msg[MAX_EVENTS];

void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg)
{
    if (event >= 0 && event < MAX_EVENTS) {
        s_event_queue[event] = mq;
        s_event_msg[event] = msg;
    }
}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{
    (void)retraceCount; // Every vblank, regardless of the requested divisor
                         // -- this game only ever asks for 1 anyway.
    osSetEventMesg(OS_EVENT_VI, mq, msg);
}

// Emulates the N64's VI retrace interrupt: delivers one message per retrace
// to the registered VI queue (and SM64's two vblank handlers). Called from
// osRecvMesg's blocking path so a single-threaded receive on a vblank-fed
// queue still terminates.
//
// Counted from the video core's frame_counter, not its vblank_triggered
// flag. That flag clears on ANY read of the register, and frame.c reads the
// same register (frame_counter) in its flip loop, so most edges were lost
// here; and retraces that passed while the game was computing were lost too,
// where the real VI interrupt would have posted them regardless.
// display_and_vsync() waits for two retraces per frame (SM64's 30 fps cap),
// so at ~13 fps each frame then sat out one or two more real vblanks: ~30%
// of the game CPU's time went to this loop. Up to two
// retraces that elapsed while nobody was waiting are now delivered at once;
// a game running faster than 30 fps still waits for real ones.
static void vi_pump(void)
{
    static uint32_t s_vi_last;
    static bool s_vi_init;
    uint32_t fc = apf_video_video_frame_counter_read();
    if (!s_vi_init) { s_vi_last = fc; s_vi_init = true; return; }
    uint32_t lag = (fc - s_vi_last) & 0x3FFFFFFFu;     /* 30-bit counter */
    if (lag == 0) return;
    s_vi_last = lag > 2u ? fc - 1u : s_vi_last + 1u;   /* keep at most 2 in hand */
    {
        if (s_event_queue[OS_EVENT_VI] != NULL) {
            mq_push(s_event_queue[OS_EVENT_VI], s_event_msg[OS_EVENT_VI]);
        }
#ifdef PORT_FULL_GAME
        /* display_and_vsync() blocks on gGfxVblankQueue (fed by the graphics
         * task via exec_dl_wrap.c) and twice on gGameVblankQueue -- the
         * latter is gVblankHandler2's queue, normally fed by handle_vblank(). */
        if (gVblankHandler1 != NULL) {
            mq_push(gVblankHandler1->queue, gVblankHandler1->msg);
        }
        if (gVblankHandler2 != NULL) {
            mq_push(gVblankHandler2->queue, gVblankHandler2->msg);
        }
#endif
    }
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    (void)flag;
    mq_push(mq, msg);
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    OSMesg tmp;
    if (mq_pop(mq, &tmp)) {
        if (msg) {
            *msg = tmp;
        }
        return 0;
    }
    if (flag == OS_MESG_NOBLOCK) {
        return -1;
    }
    // Blocking receive on an empty queue: nothing else runs concurrently to
    // produce a message (see file header), so actively pump the one real
    // event source this game waits on (vblank) until this queue has
    // something. A queue that isn't vblank-fed and is empty here would spin
    // forever -- true of real hardware too if nothing ever sends to it.
    uint32_t vp = 0;
    for (;;) {
        vi_pump();
        if (mq_pop(mq, &tmp)) {
            if (msg) {
                *msg = tmp;
            }
            return 0;
        }
        if (++vp == 30000000u) {
            printf("MQ: osRecvMesg q=%p TIMEOUT -- returning -1\n", (void *)mq);
            return -1;
        }
    }
}

// ---- Threads (see file header for the synchronous-call model) ---------------

// OSThread (os_thread.h) is a real MIPS register-context struct with no
// spare field to stash a C function pointer + arg in between
// osCreateThread and the later osStartThread call, so a small side table
// (keyed by thread pointer identity) holds them instead. 8 entries covers
// this game's real thread count (4) with headroom.
#define MAX_THREADS 8
static OSThread *s_thread_key[MAX_THREADS];
static void (*s_thread_entry[MAX_THREADS])(void *);
static void *s_thread_arg[MAX_THREADS];

void osCreateThread(OSThread *thread, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri)
{
    (void)sp;
    thread->id = id;
    thread->priority = pri;

    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_thread_key[i] == NULL || s_thread_key[i] == thread) {
            s_thread_key[i] = thread;
            s_thread_entry[i] = entry;
            s_thread_arg[i] = arg;
            return;
        }
    }
}

void osStartThread(OSThread *thread)
{
    // The sound thread's work runs from the 60 Hz audio tick instead
    // (hal/audio_hal.c). thread4_sound itself loops forever -- and since
    // osStartThread is synchronous here, thread3_main starts it BEFORE
    // thread5_game_loop, so the game would never run. Skip it.
    // (id 4 == the sound thread, create_thread(&gSoundThread, 4, ...).)
    if (thread->id == 4) {
        return;
    }

    // Directly calls the thread body -- see file header. For this game's
    // actual thread graph (idle -> main -> sound + game_loop, the latter an
    // infinite per-frame loop), this never returns once the game loop
    // thread starts, which matches what should happen: everything above it
    // in the call chain was just one-time boot sequencing, not something
    // that needed to keep running concurrently.
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_thread_key[i] == thread) {
            s_thread_entry[i](s_thread_arg[i]);
            return;
        }
    }
}

void osSetThreadPri(OSThread *thread, OSPri pri)
{
    if (thread) {
        thread->priority = pri;
    }
}

// ---- Controller (wraps hal/controller.c's APF_INPUT polling) ----------------

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data)
{
    (void)mq;
    if (bitpattern) {
        *bitpattern = 0x01; // Controller 1 present.
    }
    if (data) {
        memset(data, 0, sizeof(OSContStatus));
        data[0].type = CONT_TYPE_NORMAL;
        data[0].status = 0;
    }
    return 0;
}

s32 osContStartReadData(OSMesgQueue *mq)
{
    // Synchronous underneath (see hal_read_controller) -- if the caller's
    // pattern is "start read, then osRecvMesg to know it's done", deliver
    // the completion immediately rather than requiring a real SI event.
    if (mq) {
        mq_push(mq, NULL);
    }
    return 0;
}

void osContGetReadData(OSContPad *pad)
{
    hal_read_controller(&pad[0], 0);
}

// ---- Audio: libultra's sequence-file and AI entry points (the sound itself
// is hal/audio_hal.c) ----

/* libultra's alSeqFileNew: the table holds offsets relative to the file;
 * rebase them onto where the file sits. (This used to zero the table, which
 * left every sequence unloadable -- harmless only while nothing played.) */
void alSeqFileNew(ALSeqFile *f, u8 *base)
{
    for (s32 i = 0; i < f->seqCount; i++)
        f->seqArray[i].offset = (u8 *)((uintptr_t)f->seqArray[i].offset + (uintptr_t)base);
}

/* Silent AI for builds without sound (the host sim); hal/audio_hal.c overrides these. */
__attribute__((weak)) s32 osAiSetFrequency(u32 frequency)
{
    (void)frequency;
    return 0;
}

__attribute__((weak)) s32 osAiSetNextBuffer(void *buf, u32 size)
{
    (void)buf; (void)size;
    return 0;
}

__attribute__((weak)) u32 osAiGetLength(void)
{
    return 0;
}
