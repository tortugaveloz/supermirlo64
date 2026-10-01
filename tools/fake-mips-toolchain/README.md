# fake-mips-toolchain

The decomp's own `Makefile` unconditionally errors out at parse time
(`Unable to detect a suitable MIPS toolchain installed`, `Makefile:317`) if it
can't find `mips-linux-gnu-ld`/`mips64-linux-gnu-ld`/`mips64-elf-ld` on
`PATH` — this check runs before *any* target is built, regardless of which
target you actually asked for.

We don't want a real MIPS toolchain: we're not building the N64 ROM. We only
want the upstream Makefile to run its architecture-agnostic asset-generation
steps (charmap/text-string encoding, `level_headers.h`, and PNG-to-`.inc.c`
texture conversion via their `n64graphics` host tool) so the *portable C*
game/engine sources `#include` real generated data instead of failing on a
missing header. Those steps don't invoke `$(CC)`/`$(AS)`/`$(LD)` at all.

These stub scripts exist purely to satisfy `find-command` during that
detection check. Requesting a specific `build/us/**/*.o` target lets GNU Make
generate all of that target's real prerequisites (the `.inc.c` files we
actually want) before it reaches the final "compile with $(CC)" recipe step —
at which point one of these stubs runs, prints a message, and exits 1. That
final failure is expected and harmless: we throw away the recipe's own
compile step and recompile the same reused sources ourselves, for Mirlo's
game CPU, in the top-level `Makefile`.

Do not use these to actually build anything: the real compiler is MIRLO's
(`MIRLO/lang/mips/mips.mk`).
