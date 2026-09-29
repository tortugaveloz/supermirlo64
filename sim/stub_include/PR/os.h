/* Host-sim shadow of the decomp's monolithic <PR/os.h>. That header re-declares
 * osVirtualToPhysical / osPi* with N64 `u32` addr types that clash with the
 * split <PR/os_*.h> `uintptr_t` decls once uintptr_t != u32 (i.e. any 64-bit
 * host). ultra64.h already pulls every split header we actually need. */
#ifndef _OS_H_
#define _OS_H_
#include <ultra64.h>
#endif
