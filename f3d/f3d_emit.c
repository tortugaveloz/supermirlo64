/* See f3d_emit.h. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "geom_pipeline.h"   /* GEOM_FB_HRES */
#include "f3d_emit.h"

/* Where the big tables go: plain .bss on the game CPU; MIRLO's geom core
 * (which can run this translator itself, GDL_F3D) puts them in SDRAM --
 * its own RAM is 16 KiB. */
#ifndef F3D_BIG
#define F3D_BIG
#endif

/* a runaway guard: commands walked this display list */
static long     s_walk_iters;
#define WALK_ITER_CAP  400000L
#ifdef F3D_HOST
/* host simulator: DLs live in malloc'd memory, not the 0x4000_0000 SDRAM window */
#define IN_SDRAM(p)    ((p) != NULL)
#else
#define IN_SDRAM(p)    ((uintptr_t)(p) >= 0x40000000u && (uintptr_t)(p) < 0x44000000u)
#endif

/* F3DEX2 opcode values (gbi.h) */
#define OP_VTX 0x01
#define OP_TRI1 0x05
#define OP_TRI2 0x06
#define OP_POPMTX 0xd8
#define OP_GEOMETRYMODE 0xd9
#define OP_MTX 0xda
#define OP_MOVEWORD 0xdb
#define OP_MOVEMEM 0xdc
#define OP_TEXTURE 0xd7
#define OP_DL 0xde
#define OP_ENDDL 0xdf
#define OP_SETTILESIZE 0xf2
#define OP_SETTIMG 0xfd
#define OP_SETTILE 0xf5
#define OP_LOADBLOCK 0xf3
#define OP_LOADTLUT 0xf0
#define OP_SETCOMBINE 0xfc
#define OP_SETPRIMCOLOR 0xfa
#define OP_SETENVCOLOR 0xfb
#define OP_SETOTHERMODE_H 0xe3   /* cycle type [21:20]: copy mode steps 4 texels per dsdx unit */
#define OP_TEXRECT 0xe4          /* + G_RDPHALF_1 (s, t) + G_RDPHALF_2 (dsdx, dtdy) */
#define OP_SETOTHERMODE_L 0xe2   /* F3DEX2 (this port builds with F3DEX_GBI_2): render mode, alpha compare, z source */
#define OP_FILLRECT 0xf6         /* w0 lrx, lry  w1 ulx, uly (10.2); inclusive in fill mode */
#define OP_SETFILLCOLOR 0xf7     /* w1: two RGBA5551 pixels (16-bit colour image) */
#define OP_SETZIMG 0xfe
#define OP_SETCIMG 0xff
#define OP_SPNOOP 0xe0            /* F3DEX2 G_SPNOOP */

#define MTX_PUSH 0x01
#define MTX_LOAD 0x02
#define MTX_PROJECTION 0x04
#define MV_VIEWPORT 0x08
#define MV_LIGHT    0x0a           /* G_MV_LIGHT (F3DEX2) */
#define MW_NUMLIGHT 0x02           /* G_MW_NUMLIGHT */
#define MW_LIGHTCOL 0x0a           /* G_MW_LIGHTCOL */
#define GEO_LIGHTING 0x00020000u
#define GEO_CULL_BACK 0x00000400u   /* F3DEX2 G_CULL_BACK */
#define GEO_TEXTURE_GEN 0x00040000u /* F3DEX2 G_TEXTURE_GEN: s,t from the normals (env map) */

#define DL_DEPTH 16

/* Fill rectangles. SM64 paints a level's background colour (GEO_BACKGROUND_
 * COLOR -- white on the act selector, whose text is black) and clears the
 * screen with full-screen fills in fill mode; the one that comes before any
 * drawing is the frame's clear colour, which the caller applies to the clear
 * it does anyway (f3d_clear_*), so it costs no fill rate. Any other fill is
 * drawn. A fill whose colour image is the depth buffer (init_z_buffer) is
 * the frame's own depth clear and is dropped. */
int      f3d_clear_valid;     /* set by this display list's background fill */
uint32_t f3d_clear_rgb;       /* 0xRRGGBB */
/* The emitter's semantic state: everything a display list's translation
 * depends on (texture, tiles, combiner, render mode, geometry mode, lights).
 * One struct, so the display-list cache (call_dl) can hash and compare it in
 * place; the old names stay as macros. */
typedef struct {
    uint32_t  geomode, tex_render_size, texenv, cc_w0, cc_w1, env_rgba, prim_rgba;
    uint32_t  othermode_h, othermode_l, rm_flags, fill_color;
    uint32_t  lightcol[8];
    int32_t   tex_on, numlights, timg_fmt, timg_siz, tile_cms, tile_cmt, tile_masks, tile_maskt;
    int32_t   tile_pal, ts_w, ts_h, tile_fmt, tile_siz, tile_valid, texbind_dirty;
    uint32_t  tex_sscale, tex_tscale;   /* gsSPTexture's scales, S15.16 (0x10000 = 1.0) */
    uintptr_t timg_ptr, tlut_ptr;
} f3d_sem_t;
static f3d_sem_t s_sem = {
    .env_rgba = 0xffffffffu, .prim_rgba = 0xffffffffu,
    .tex_sscale = 0x10000u, .tex_tscale = 0x10000u,
};
#define s_geomode         (s_sem.geomode)
#define s_tex_render_size (s_sem.tex_render_size)
#define s_texenv          (s_sem.texenv)
#define s_cc_w0           (s_sem.cc_w0)
#define s_cc_w1           (s_sem.cc_w1)
#define s_env_rgba        (s_sem.env_rgba)
#define s_prim_rgba       (s_sem.prim_rgba)
#define s_othermode_h     (s_sem.othermode_h)
#define s_othermode_l     (s_sem.othermode_l)
#define s_rm_flags        (s_sem.rm_flags)
#define s_fill_color      (s_sem.fill_color)
#define s_lightcol        (s_sem.lightcol)
#define s_tex_on          (s_sem.tex_on)
#define s_numlights       (s_sem.numlights)
#define s_timg_fmt        (s_sem.timg_fmt)
#define s_timg_siz        (s_sem.timg_siz)
#define s_tile_cms        (s_sem.tile_cms)
#define s_tile_cmt        (s_sem.tile_cmt)
#define s_tile_masks      (s_sem.tile_masks)
#define s_tile_maskt      (s_sem.tile_maskt)
#define s_tile_pal        (s_sem.tile_pal)
#define s_ts_w            (s_sem.ts_w)
#define s_ts_h            (s_sem.ts_h)
#define s_tile_fmt        (s_sem.tile_fmt)
#define s_tile_siz        (s_sem.tile_siz)
#define s_tile_valid      (s_sem.tile_valid)
#define s_texbind_dirty   (s_sem.texbind_dirty)
#define s_tex_sscale      (s_sem.tex_sscale)
#define s_tex_tscale      (s_sem.tex_tscale)
#define s_timg_ptr        (s_sem.timg_ptr)
#define s_tlut_ptr        (s_sem.tlut_ptr)
/* s_fill_color: s_sem */
static uintptr_t s_cimg, s_zimg;
static int       s_drawn;     /* a triangle or rectangle has gone out */

/* tracked state */
/* s_geomode: s_sem */
static int      s_lighting_emitted;
/* s_tex_render_size: s_sem */
/* s_tex_on: s_sem */
/* s_numlights (directional light count, G_MW_NUMLIGHT): s_sem */
/* s_lightcol[8] (0x00RRGGBB, overrides via G_MW_LIGHTCOL): s_sem */

/* texture tile / image state (F3DEX2 SETTIMG/SETTILE/SETTILESIZE/LOADTLUT) */
/* s_timg_ptr: s_sem */
/* s_timg_fmt, s_timg_siz: s_sem */
/* s_tile_cms, _cmt, _masks, _maskt, _pal: s_sem */
/* s_ts_w, s_ts_h: s_sem */
/* s_tlut_ptr: s_sem */
/* s_tile_fmt, s_tile_siz, s_tile_valid (the RENDER tile's texel format): s_sem */
/* s_texbind_dirty: s_sem */
static int       s_texbind_emitted;
/* s_tex_sscale, s_tex_tscale (gsSPTexture scale, 1.0 = 0xFFFF): s_sem */
/* combiner, reduced to a GDL_TEXENV mode (combine_to_texenv()) */
static uint32_t  s_texenv_emitted;   /* s_texenv: s_sem */
/* the combiner's constant inputs (combine_to_ccolor()) */
/* s_cc_w0, s_cc_w1 (the last SETCOMBINE): s_sem */
/* s_env_rgba, s_prim_rgba: s_sem */
static uint64_t  s_cc_emitted;                /* flags << 32 | rgba last sent; ~0 = none */
/* what G_TEXRECT has to put back: the projection (LOADs and MULs replayed
 * here), the viewport, and othermode H's cycle type */
static int32_t   s_proj[16];
static int       s_proj_valid;
static int       s_proj_ortho;       /* the projection is orthographic: 2D */
/* 2D translations folded into the vertices: mv_load() */
static int       s_dlc_translating;  /* (the DL cache's, below) */
static int32_t   s_emv[16], s_mv_true[16];
static int       s_emv_valid, s_fold;
static int32_t   s_fold_d[3];        /* whole units, added to every vertex */
/* The modelview stack, mirrored: GDL_MTX_MUL multiplies target x payload,
 * the RSP's G_MTX_MUL payload x target (SM64's menus chain translate then
 * scale this way -- the dialog box sat at the scaled offset, above its
 * text). So a G_MTX_MUL goes to the geom core as the LOAD of the product. */
#define F3D_MV_DEPTH 32
static int32_t   s_mv[F3D_MV_DEPTH][16] F3D_BIG;
static int       s_mv_sp;
static int32_t   s_vp_scale[4], s_vp_trans[4];   /* S15.16 */
static int       s_vp_valid;
/* s_othermode_h: s_sem */
/* othermode L (render mode) and the GDL_RM_* flags derived from it */
/* s_othermode_l: s_sem */
static uint32_t  s_rm_emitted;   /* s_rm_flags: s_sem */

/* a = b x a for 4x4 S15.16 (row vectors: the N64's G_MTX_MUL order) */
static void mtx_premul_fx(int32_t a[16], const int32_t b[16]) {
    int32_t r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            int64_t acc = 0;
            for (int k = 0; k < 4; k++) acc += (int64_t)b[i * 4 + k] * a[k * 4 + j];
            r[i * 4 + j] = (int32_t)(acc >> 16);
        }
    for (int i = 0; i < 16; i++) a[i] = r[i];
}

static void sem_dirty(void);
void f3d_emit_reset(void) {
    sem_dirty();
    s_geomode = 0;
    s_lighting_emitted = -1;
    s_tex_render_size = 0;
    s_tex_on = 0;
    s_numlights = 1;
    for (int i = 0; i < 8; i++) s_lightcol[i] = 0;
    s_timg_ptr = 0; s_timg_fmt = 0; s_timg_siz = 2;
    s_tile_cms = s_tile_cmt = 0; s_tile_masks = s_tile_maskt = 0; s_tile_pal = 0;
    s_ts_w = s_ts_h = 0; s_tlut_ptr = 0; s_tile_valid = 0;
    s_texbind_dirty = 0; s_texbind_emitted = 0;
    s_tex_sscale = s_tex_tscale = 0x10000u;
    s_proj_valid = 0; s_vp_valid = 0; s_proj_ortho = 0;
    s_emv_valid = 0; s_fold = 0;
    s_othermode_h = 2u << 12;   /* G_TF_BILERP until the frame's init_rdp says otherwise */
    s_mv_sp = 0;
    for (int i = 0; i < 16; i++) s_mv[0][i] = (i % 5) ? 0 : 65536;
    /* -1: the geom core keeps this across frames, so state it explicitly. */
    s_texenv = 0; s_texenv_emitted = 0xFFFFFFFFu;
    s_cc_w0 = s_cc_w1 = 0; s_env_rgba = s_prim_rgba = 0xffffffffu; s_cc_emitted = ~0ull;
    s_othermode_l = 0;
    s_rm_flags = GDL_RM_DEFAULT; s_rm_emitted = 0xFFFFFFFFu;
}

/* The N64 render mode (othermode L bits 3..31) -> GDL_RM_* flags:
 *   Z_CMP 0x10 / Z_UPD 0x20       depth test / depth write
 *   ZMODE 0xC00 == ZMODE_DEC      decal: drawn onto a coplanar surface
 *   FORCE_BL 0x4000 with the blender's second term CLR_MEM * (1 - A_IN)
 *                                 translucent: in*a + mem*(1-a)
 * The blender fields are checked for either cycle (GBL_c1 m2a/m2b at
 * [23:22]/[19:18], GBL_c2 at [21:20]/[17:16]); SM64's translucent modes set
 * both (G_RM_AA_ZB_XLU_SURF, _DECAL, _INTER). Opaque modes also read memory
 * (CLR_MEM * A_MEM, for antialiasing) but without FORCE_BL and with A_MEM,
 * so they stay opaque. */
static uint32_t rm_flags_of(uint32_t om) {
    uint32_t f = 0;
    if (om & 0x10u) f |= GDL_RM_ZCMP;
    if (om & 0x20u) f |= GDL_RM_ZUPD;
    if ((om & 0xC00u) == 0xC00u) f |= GDL_RM_DECAL;
    int c1 = ((om >> 22) & 3u) == 1u /* CLR_MEM */ && ((om >> 18) & 3u) == 0u /* 1MA */;
    int c2 = ((om >> 20) & 3u) == 1u && ((om >> 16) & 3u) == 0u;
    if ((om & 0x4000u) && (c1 || c2)) f |= GDL_RM_XLU;
    return f;
}

static void emit_rendermode_if_changed(gdl_cur_t *out) {
    if (s_rm_flags != s_rm_emitted) { gdl_rendermode(out, s_rm_flags); s_rm_emitted = s_rm_flags; }
}

/* Emit GDL_TEXBIND for the current tile if texturing is on and something
 * changed since the last emit. Called just before a triangle. */
static void emit_texbind_if_needed(gdl_cur_t *out) {
    if (!s_tex_on || !s_timg_ptr) return;
    if (!s_texbind_dirty && s_texbind_emitted) return;
    int w = s_ts_w > 0 ? s_ts_w : (s_tile_masks > 0 ? (1 << s_tile_masks) : 32);
    int h = s_ts_h > 0 ? s_ts_h : (s_tile_maskt > 0 ? (1 << s_tile_maskt) : 32);
    /* fmt/siz come from the render tile, NOT from SETTIMG. The image
     * descriptor carries the LOAD size, and G_IM_SIZ_4b_LOAD_BLOCK and
     * G_IM_SIZ_8b_LOAD_BLOCK are both G_IM_SIZ_16b: an 8-bit texture is
     * loaded as if it were 16-bit and only the tile that renders it states
     * the real texel size. Taking it from SETTIMG decoded Mario's IA8 shadow
     * as IA16 -- with twice the stride -- and drew four stray dots. */
    /* othermode H: copy mode [21:20] == 2 or texture filter [13:12] == G_TF_POINT */
    int point = ((s_othermode_h >> 20) & 3u) == 2u || ((s_othermode_h >> 12) & 3u) == 0u;
    gdl_texbind_scaled_fx(out, s_tile_valid ? s_tile_fmt : s_timg_fmt,
                s_tile_valid ? s_tile_siz : s_timg_siz, s_tile_cms, s_tile_cmt,
                s_tile_pal | (point ? GDL_TEXBIND_POINT : 0),
                w, h, (const void *)s_timg_ptr, (const void *)s_tlut_ptr,
                s_tex_sscale, s_tex_tscale);
    s_texbind_dirty = 0;
    s_texbind_emitted = 1;
}

/* SM64 Mtx: 16 s16 integer parts then 16 u16 fraction parts;
 * element = (s16<<16 | u16) / 65536.
 *
 * mtxf_to_mtx() -> guMtxF2L() (AVOID_UB path) packs each *adjacent element
 * pair* byte-swapped: it writes one s32 per pair as (hi16(mf[r][2c])<<16) |
 * hi16(mf[r][2c+1]), so on a little-endian target the s16 at array index i
 * actually holds mf element i^1. guMtxL2F() is the matching un-packer. Reading
 * hi[i]/lo[i] linearly transposes columns 0<->1 and 2<->3 of every matrix
 * (found via the host simulator: made clip.w collapse to a constant). */
static void mtx_to_fixed(const void *mtx, int32_t out[16]) {
    const int16_t  *hi = (const int16_t  *)mtx;
    const uint16_t *lo = (const uint16_t *)mtx + 16;
    for (int i = 0; i < 16; i++) {
        int j = i ^ 1;
        out[i] = (int32_t)(((uint32_t)(uint16_t)hi[j] << 16) | lo[j]);   /* S15.16, the GDL's format */
    }
}

static int texgen_as_written(void);
static int texgen_shade_only(void);
static void emit_texenv_if_changed(gdl_cur_t *out) {
    /* G_TEXTURE_GEN: the geom core generates s,t from the normals
     * (GDL_GEOMODE_TEXGEN), so the combiner applies as written -- the power
     * star is G_CC_DECALFADE, all of its yellow is the env-mapped texture. */
    uint32_t te = texgen_shade_only() ? (GDL_TE_RGB_SHADE | GDL_TE_A_SHADE) : s_texenv;
    if (te != s_texenv_emitted) { gdl_texenv(out, te); s_texenv_emitted = te; }
}

/* The N64 colour combiner, (A - B) * C + D per channel group, reduced to
 * the GDL_TEXENV modes the geometry core handles (geom_gdl.h). Only cycle 0
 * is read: SM64 draws in 1-cycle mode, where both cycles are set equal.
 * Field layout is gbi.h's GCCc0w0/GCCc0w1. PRIMITIVE and ENVIRONMENT (and
 * their alphas) count as the shade side of a product: combine_to_ccolor()
 * folds their values into the vertex colour through GDL_CCOLOR, so "shade"
 * below means "the vertex colour after that". (They used to be taken as 1,
 * which drew SM64's flames -- texel * env -- as bare grey IA texels.)
 *   rgb   (TEXEL0 - SHADE) * TEXEL0_ALPHA + SHADE  -> LERP  (G_CC_BLENDRGB*)
 *         a product with no texel term             -> SHADE (G_CC_SHADE*)
 *         a product with no shade term             -> TEXEL (G_CC_DECALRGB*)
 *         anything else                            -> MODULATE
 *   alpha the same, without the lerp.
 * The alpha mode matters twice: SM64's snowflakes (G_CC_DECALRGBA) have
 * grey 0x7F vertices and are white on an N64 -- modulated they came out
 * half as bright -- and a texel-derived alpha is what turns the alpha test
 * on for cut-out edges, where G_CC_MODULATERGB's shade alpha must not. */
#define CCMUX_TEXEL0        1
#define CCMUX_SHADE         4
#define CCMUX_TEXEL0_ALPHA  8
#define CCMUX_0_A          15   /* G_CCMUX_0 in the 4-bit A/B fields */
#define CCMUX_0_C          31
#define CCMUX_0_D           7
#define ACMUX_0             7
#define CCMUX_PRIM          3
#define CCMUX_ENV           5
#define CCMUX_PRIM_ALPHA   10
#define CCMUX_ENV_ALPHA    12
/* The per-vertex colour side of a product: SHADE, or a PRIMITIVE/ENVIRONMENT
 * constant, which GDL_CCOLOR folds into the vertex colour. */
static int cc_is_k(uint32_t x, uint32_t shade)
{
    return x == shade || x == CCMUX_PRIM || x == CCMUX_ENV
        || (shade == CCMUX_SHADE && (x == CCMUX_PRIM_ALPHA || x == CCMUX_ENV_ALPHA));
}
/* What a product x * y (or a lone term x, y = 1) draws on: bit0 texel, bit1 shade. */
static uint32_t cc_terms(uint32_t x, uint32_t y, uint32_t texel, uint32_t shade)
{
    return (x == texel || y == texel ? 1u : 0u) | (cc_is_k(x, shade) || cc_is_k(y, shade) ? 2u : 0u);
}
static uint32_t cc_mode(uint32_t terms, uint32_t modulate, uint32_t texel, uint32_t shade)
{
    return terms == 3u ? modulate : terms == 1u ? texel : shade;   /* constants alone: shade */
}
/* G_TEXTURE_GEN applies as written only when the colour combiner has a form
 * TexEnv reproduces -- D alone, A * C, or the texel-alpha lerp (the power
 * star's G_CC_DECALFADE). The general (A - B) * C + D -- Goddard's face shine,
 * G_CC_HILITERGBA = (PRIM - SHADE) * TEXEL0 + SHADE -- would come out as
 * texel * PRIM, black on the title screen; such a surface keeps the lit
 * shade without its highlight, as before texgen existed. */
static int texgen_as_written(void)
{
    uint32_t a = (s_cc_w0 >> 20) & 0xf, c = (s_cc_w0 >> 15) & 0x1f;
    uint32_t b = ((uint32_t)s_cc_w1 >> 28) & 0xf, d = ((uint32_t)s_cc_w1 >> 15) & 0x7;
    if (!(s_geomode & GEO_TEXTURE_GEN)) return 0;
    return (a == CCMUX_TEXEL0 && b == CCMUX_SHADE && c == CCMUX_TEXEL0_ALPHA && d == CCMUX_SHADE)
        || c == CCMUX_0_C || a == b || (b == CCMUX_0_A && d == CCMUX_0_D);
}
static int texgen_shade_only(void) { return (s_geomode & GEO_TEXTURE_GEN) && !texgen_as_written(); }
static uint32_t combine_to_texenv(uint32_t w0, uint32_t w1) {
    uint32_t a = (w0 >> 20) & 0xf, c = (w0 >> 15) & 0x1f;
    uint32_t b = (w1 >> 28) & 0xf, d = (w1 >> 15) & 0x7;
    uint32_t aa = (w0 >> 12) & 0x7, ac = (w0 >> 9) & 0x7;
    uint32_t ab = (w1 >> 12) & 0x7, ad = (w1 >> 9) & 0x7;
    uint32_t rgb, alpha;

    if (a == CCMUX_TEXEL0 && b == CCMUX_SHADE && c == CCMUX_TEXEL0_ALPHA && d == CCMUX_SHADE)
        rgb = GDL_TE_RGB_LERP;
    else if (c == CCMUX_0_C || a == b)              /* the product vanishes: D alone */
        rgb = cc_mode(cc_terms(d, d, CCMUX_TEXEL0, CCMUX_SHADE),
                      GDL_TE_RGB_SHADE, GDL_TE_RGB_TEXEL, GDL_TE_RGB_SHADE);
    else if (b == CCMUX_0_A && d == CCMUX_0_D)      /* A * C */
        rgb = cc_mode(cc_terms(a, c, CCMUX_TEXEL0, CCMUX_SHADE),
                      GDL_TE_RGB_MODULATE, GDL_TE_RGB_TEXEL, GDL_TE_RGB_SHADE);
    else
        rgb = GDL_TE_RGB_MODULATE;

    if (ac == ACMUX_0 || aa == ab)
        alpha = cc_mode(cc_terms(ad, ad, CCMUX_TEXEL0, CCMUX_SHADE),
                        GDL_TE_A_SHADE, GDL_TE_A_TEXEL, GDL_TE_A_SHADE);
    else if (ab == ACMUX_0 && ad == ACMUX_0)
        alpha = cc_mode(cc_terms(aa, ac, CCMUX_TEXEL0, CCMUX_SHADE),
                        GDL_TE_A_MODULATE, GDL_TE_A_TEXEL, GDL_TE_A_SHADE);
    else
        alpha = GDL_TE_A_MODULATE;
    return rgb | alpha;
}

/* The constants the same combiner multiplies by, as GDL_CCOLOR (geom_gdl.h):
 * each channel group's product terms (A * C, or D alone -- the same reading
 * combine_to_texenv() makes) that are PRIMITIVE / ENVIRONMENT (or their
 * alphas) multiply into K, and whether SHADE is one of them decides if the
 * vertex colour is kept or replaced by K. This is what makes the flames
 * (G_CC_FADEA: texel * env) orange instead of the bare grey IA texel, and
 * env-alpha fades (Boos, transitions) fade. */
static uint32_t cc_ch(uint32_t rgba, int sh) { return (rgba >> sh) & 0xffu; }
/* One term of a product: SHADE sets *shade, a constant multiplies into k and
 * returns 1. (The alpha mux codes for PRIMITIVE/ENVIRONMENT/SHADE are the
 * same numbers as the colour ones.) */
static int cc_fold(uint32_t x, uint32_t k[4], int *shade, int alpha_group)
{
    uint32_t src = 0; int use_a = 0;
    if (x == CCMUX_PRIM) src = s_prim_rgba;
    else if (x == CCMUX_ENV) src = s_env_rgba;
    else if (!alpha_group && x == CCMUX_PRIM_ALPHA) { src = s_prim_rgba; use_a = 1; }
    else if (!alpha_group && x == CCMUX_ENV_ALPHA)  { src = s_env_rgba;  use_a = 1; }
    else { if (x == CCMUX_SHADE) *shade = 1; return 0; }
    if (alpha_group) { k[3] = k[3] * cc_ch(src, 0) / 255u; return 1; }
    for (int c = 0; c < 3; c++)
        k[c] = k[c] * (use_a ? cc_ch(src, 0) : cc_ch(src, 24 - 8 * c)) / 255u;
    return 1;
}
static uint64_t combine_to_ccolor(uint32_t w0, uint32_t w1)
{
    uint32_t a = (w0 >> 20) & 0xf, c = (w0 >> 15) & 0x1f;
    uint32_t b = (w1 >> 28) & 0xf, d = (w1 >> 15) & 0x7;
    uint32_t aa = (w0 >> 12) & 0x7, ac = (w0 >> 9) & 0x7;
    uint32_t ab = (w1 >> 12) & 0x7, ad = (w1 >> 9) & 0x7;
    uint32_t k[4] = { 255u, 255u, 255u, 255u };
    int shade_rgb = 0, shade_a = 0, konst = 0;

    if (a == CCMUX_TEXEL0 && b == CCMUX_SHADE && c == CCMUX_TEXEL0_ALPHA && d == CCMUX_SHADE)
        shade_rgb = 1;                                  /* the lerp: shade as is */
    else if (c == CCMUX_0_C || a == b)
        konst |= cc_fold(d, k, &shade_rgb, 0);
    else { konst |= cc_fold(a, k, &shade_rgb, 0); konst |= cc_fold(c, k, &shade_rgb, 0); }
    if (ac == ACMUX_0 || aa == ab)
        konst |= cc_fold(ad, k, &shade_a, 1);
    else { konst |= cc_fold(aa, k, &shade_a, 1); konst |= cc_fold(ac, k, &shade_a, 1); }
    if (!konst) return 0;     /* shade and texel only: vertex colours untouched */
    /* a group with neither shade nor a constant (the texel alone, or 1)
     * leaves the vertex colour as it is -- the texenv mode ignores it there */
    if (!shade_rgb && k[0] == 255u && k[1] == 255u && k[2] == 255u
        && (combine_to_texenv(w0, w1) & 3u) == GDL_TE_RGB_TEXEL) shade_rgb = 1;
    return ((uint64_t)(1u | (shade_rgb ? 2u : 0u) | (shade_a ? 4u : 0u)) << 32)
         | (k[0] << 24) | (k[1] << 16) | (k[2] << 8) | k[3];
}
static void emit_ccolor_if_changed(gdl_cur_t *out) {
    /* combine_to_ccolor() divides by 255 per channel: worth ~5 % of the game
     * CPU when it ran for every triangle. Its inputs are the combiner words,
     * the two colours and texgen, so it is recomputed only when one changes. */
    static uint32_t k_w0, k_w1, k_env, k_prim; static int k_tg = -1;
    static uint64_t k_cc;
    int tg = texgen_shade_only();
    if (tg != k_tg || s_cc_w0 != k_w0 || s_cc_w1 != k_w1 || s_env_rgba != k_env || s_prim_rgba != k_prim) {
        k_tg = tg; k_w0 = s_cc_w0; k_w1 = s_cc_w1; k_env = s_env_rgba; k_prim = s_prim_rgba;
        k_cc = tg ? 0 : combine_to_ccolor(s_cc_w0, s_cc_w1);
    }
    uint64_t cc = k_cc;
    if (cc != s_cc_emitted) {
        gdl_ccolor(out, (uint32_t)(cc >> 32), (uint32_t)cc);
        s_cc_emitted = cc;
    }
}

static void emit_geomode_if_changed(gdl_cur_t *out) {
    /* lighting, and whether back faces are culled: SM64 clears G_CULL_BACK
     * for two-sided surfaces (HMC's climbable ceiling mesh, seen from below) */
    int gm = ((s_geomode & GEO_LIGHTING) ? GDL_GEOMODE_LIGHTING : 0)
           | ((s_geomode & GEO_CULL_BACK) ? 0 : GDL_GEOMODE_NOCULL)
           | (texgen_as_written() ? GDL_GEOMODE_TEXGEN : 0);
    if (gm != s_lighting_emitted) { gdl_geomode(out, gm); s_lighting_emitted = gm; }
}
static void emit_texnorm(gdl_cur_t *out) {
    gdl_texnorm_size(out, (s_tex_on && s_tex_render_size > 0) ? (uint32_t)s_tex_render_size : 0u);
}

/* G_TEXRECT: a screen-space textured rectangle (the HUD, menu text, ...).
 * The geom core only draws triangles, so this is two of them, in N64 screen
 * space: vertices in quarter pixels (the rectangle's own 10.2 units) under a
 * temporary identity modelview and an orthographic projection with w = 640,
 * which keeps every matrix entry an integer or 4/3 -- 1/640 would not survive
 * S15.16. The full-screen viewport is set for it and the game's projection
 * and viewport are put back after; the texture is bound without
 * gsSPTexture's scale, which does not apply to rectangles. */
static void emit_viewport(gdl_cur_t *out, const int32_t scale[4], const int32_t trans[4]) {
    gdl_viewport_fx(out, scale, trans);
}

/* 2D translations folded into the vertices. SM64 draws a string glyph after
 * glyph, each under its own modelview (a translation by the glyph's advance),
 * and as a full matrix LOAD each glyph cost the geom core a 16-word compare,
 * an MVP product and its quantisation: a dialog page added ~20 % to a
 * level's frame (MIRLO's sim/geom_full). Under an orthographic projection, a
 * modelview that differs from the one the geom core holds (s_emv) only by a
 * whole-unit translation, over an identity 3x3, is not sent: the translation
 * goes into the vertices instead (OP_VTX), which lands them exactly where the
 * matrix would have. Undone (the real matrix sent) before a PUSH, since the
 * geom core copies what it holds; forgotten at a POP (the level below is
 * whatever was pushed, not tracked here). */
int f3d_fold_off;                   /* 1: every modelview as a LOAD (A/B) */
static void mv_load(gdl_cur_t *out, const int32_t m[16]) {
    for (int k = 0; k < 16; k++) s_mv_true[k] = m[k];
    if (s_proj_ortho && s_emv_valid && !s_dlc_translating && !f3d_fold_off) {
        int ok = m[0] == 65536 && m[5] == 65536 && m[10] == 65536 && m[15] == 65536
              && !(m[1] | m[2] | m[3] | m[4] | m[6] | m[7] | m[8] | m[9] | m[11]);
        for (int k = 0; k < 12 && ok; k++) ok = m[k] == s_emv[k];
        ok = ok && m[15] == s_emv[15];
        int32_t d[3];
        for (int k = 0; k < 3 && ok; k++) {
            d[k] = m[12 + k] - s_emv[12 + k];
            ok = (d[k] & 0xffff) == 0 && d[k] >= -(4096 << 16) && d[k] <= (4096 << 16);
        }
        if (ok) {
            for (int k = 0; k < 3; k++) s_fold_d[k] = d[k] >> 16;
            s_fold = (d[0] | d[1] | d[2]) != 0;
            return;
        }
    }
    gdl_mtx_load_fx(out, GDL_MTX_TARGET_MODELVIEW, m);
    for (int k = 0; k < 16; k++) s_emv[k] = m[k];
    s_emv_valid = 1; s_fold = 0;
}
static void mv_unfold(gdl_cur_t *out) {
    if (!s_fold) return;
    gdl_mtx_load_fx(out, GDL_MTX_TARGET_MODELVIEW, s_mv_true);
    for (int k = 0; k < 16; k++) s_emv[k] = s_mv_true[k];
    s_fold = 0;
}

static void emit_mv(gdl_cur_t *out) {
    mv_load(out, s_mv[s_mv_sp]);
}

/* Two triangles covering x0..x1, y0..y1 (quarter pixels, N64 screen space)
 * under a temporary identity modelview, an orthographic projection with
 * w = 640 and the full-screen viewport; the game's are put back after.
 * Vertex colour rgba (0xRRGGBBAA), texture coordinates s, t (S10.5). */
static void emit_rect_quad(gdl_cur_t *out, int32_t xl, int32_t yl, int32_t xh, int32_t yh,
                           int32_t s0, int32_t t0, int32_t s1, int32_t t1, uint32_t rgba) {
    gdl_geomode(out, GDL_GEOMODE_NOCULL);        /* unlit, two-sided */
    s_lighting_emitted = -1;

    static const int32_t ident[16] = { 65536, 0, 0, 0,  0, 65536, 0, 0,  0, 0, 65536, 0,  0, 0, 0, 65536 };
    static const int32_t ortho[16] = { 65536, 0, 0, 0,  0, -87381, 0, 0,  0, 0, 0, 0,
                                       -640 * 65536, 640 * 65536, 0, 640 * 65536 };
    static const int32_t vps[4] = { 160 << 16, 120 << 16, 511 << 14, 0 };   /* the full screen, as SM64's own viewport (127.75) */
    gdl_mtx_push(out);
    gdl_mtx_load_fx(out, GDL_MTX_TARGET_MODELVIEW, ident);
    gdl_mtx_load_fx(out, GDL_MTX_TARGET_PROJECTION, ortho);
    emit_viewport(out, vps, vps);
    geom_vtx_t v[4];
    memset(v, 0, sizeof v);
    const int32_t x[4] = { xl, xh, xh, xl }, y[4] = { yl, yl, yh, yh };
    const int32_t sc[4] = { s0, s1, s1, s0 }, tc[4] = { t0, t0, t1, t1 };
    for (int i = 0; i < 4; i++) {
        v[i].ob[0] = (int16_t)x[i]; v[i].ob[1] = (int16_t)y[i];
        v[i].tc[0] = (int16_t)sc[i]; v[i].tc[1] = (int16_t)tc[i];
        v[i].cn[0] = (uint8_t)(rgba >> 24); v[i].cn[1] = (uint8_t)(rgba >> 16);
        v[i].cn[2] = (uint8_t)(rgba >> 8);  v[i].cn[3] = (uint8_t)rgba;
    }
    gdl_vtx(out, 28, v, 4);
    gdl_tri2(out, 28, 29, 30, 28, 30, 31);
    gdl_mtx_pop(out, 1);
    if (s_proj_valid) gdl_mtx_load_fx(out, GDL_MTX_TARGET_PROJECTION, s_proj);
    if (s_vp_valid) emit_viewport(out, s_vp_scale, s_vp_trans);
}

static void emit_texrect(gdl_cur_t *out, uint32_t w0, uint32_t w1, uint32_t half1, uint32_t half2) {
    int32_t xh = (int32_t)((w0 >> 12) & 0xfff), yh = (int32_t)(w0 & 0xfff);
    int32_t xl = (int32_t)((w1 >> 12) & 0xfff), yl = (int32_t)(w1 & 0xfff);
    int32_t s0 = (int16_t)(half1 >> 16), t0 = (int16_t)half1;          /* S10.5 */
    int32_t dsdx = (int16_t)(half2 >> 16), dtdy = (int16_t)half2;      /* S5.10 */
    if (((s_othermode_h >> 20) & 3u) == 2u) {   /* copy mode: 4 texels per step, edges inclusive */
        dsdx /= 4; xh += 4; yh += 4;
    }
    if (xh <= xl || yh <= yl) return;
    /* MRDP samples at pixel centres, as the N64 does, so a 1:1 rectangle's
     * pixels land mid-texel with no adjustment. */
    int32_t s1 = s0 + ((dsdx * (xh - xl)) >> 7), t1 = t0 + ((dtdy * (yh - yl)) >> 7);

    int tex_on = s_tex_on; uint32_t ss = s_tex_sscale, ts = s_tex_tscale;
    s_tex_on = 1; s_tex_sscale = s_tex_tscale = 0x10000u; s_texbind_dirty = 1;
    /* Copy mode (the HUD) writes texels as they are: no combiner, no Z, and
     * the alpha compare drops the transparent ones -- as a texel-only blend. */
    int copy = ((s_othermode_h >> 20) & 3u) == 2u;
    /* a rectangle is the RDP's alone: the RSP's geometry mode (texture gen,
     * lighting) does not apply to it */
    uint32_t gm_saved = s_geomode;
    s_geomode &= ~GEO_TEXTURE_GEN;
    uint32_t te = s_texenv, rm = s_rm_flags;
    if (copy) {
        s_texenv = GDL_TE_RGB_TEXEL | GDL_TE_A_TEXEL;
        s_rm_flags = GDL_RM_XLU;
        if (s_cc_emitted >> 32) { gdl_ccolor(out, 0, 0); s_cc_emitted = 0; }   /* constants off */
    } else {
        emit_ccolor_if_changed(out);
    }
    emit_texenv_if_changed(out);
    emit_rendermode_if_changed(out);
    emit_texbind_if_needed(out);
    s_texenv = te; s_rm_flags = rm;
    s_geomode = gm_saved;
    s_tex_on = tex_on; s_tex_sscale = ss; s_tex_tscale = ts; s_texbind_dirty = 1;
    emit_rect_quad(out, xl, yl, xh, yh, s0, t0, s1, t1, 0xffffffffu);
}

/* ---- Translation cache for static display lists ----------------------------
 * SM64's models and level geometry are display lists in read-only memory that
 * never change, yet every frame walked them command by command out of SDRAM
 * -- on the target that is most of f3d_emit's ~35 ms (the game CPU's cache
 * misses; the x86 host does the same walk in ~25 us). The N64 did the same
 * walk every frame too, on the RSP, with DMA.
 *
 * A gSPDisplayList to a static list is translated once into a chunk of GDL in
 * the arena, ended by GDL_ENDDL, and from then on the frame's GDL only calls
 * it (GDL_DL, which the geom core already runs). The translation depends on
 * the state it starts in (texture, tiles, combiner, render mode, geometry
 * mode, lights: f3d_sem_t), so the entry state is the key along with the
 * list's address, and the state it leaves behind is stored and restored on a
 * hit. A chunk does not rely on what the geom core had been sent before: the
 * "last emitted" trackers start unknown inside it, so it states what it uses.
 *
 * Not cached: a list that loads matrices, sets a viewport, draws rectangles
 * or fills (they depend on the matrix stack and the screen layout), or whose
 * vertices, textures, lights or sub-lists are not static themselves -- such a
 * list is remembered as uncacheable and walked as before.
 *
 * The arena is two halves used in turn: when one fills up the other is
 * cleared and taken, and the chunks in the full one stay valid while the
 * frames already built on them are still being drawn (at most two frames:
 * pipelined mode), so a half is never reused within three frames. */
typedef struct {
    uint64_t cc_emitted;
    uint32_t texenv_emitted, rm_emitted;
    int32_t  lighting_emitted, texbind_emitted, drawn;
} f3d_emitted_t;
/* word-wise: picolibc's memcmp/memcpy go byte by byte, and on the target a
 * lookup's compares showed up in the profile */
#define SEM_WORDS ((uint32_t)(sizeof(f3d_sem_t) / 4u))
static int sem_eq(const f3d_sem_t *a, const f3d_sem_t *b) {
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    for (uint32_t i = 0; i < SEM_WORDS; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static void sem_copy(f3d_sem_t *d, const f3d_sem_t *s_) {
    uint32_t *x = (uint32_t *)d; const uint32_t *y = (const uint32_t *)s_;
    for (uint32_t i = 0; i < SEM_WORDS; i++) x[i] = y[i];
}
static uint32_t sem_hash(const f3d_sem_t *a) {
    const uint32_t *x = (const uint32_t *)a;
    /* a pre-filter only (sem_eq decides): rotate-xor, no multiply chain */
    uint32_t h = 0x9e3779b9u;
    for (uint32_t i = 0; i < SEM_WORDS; i++) h = ((h << 5) | (h >> 27)) ^ x[i];
    return h;
}
/* Interned states. Every state a cache entry starts or ends in is stored once
 * in the arena (sem_node_t, found by hash), so an entry holds two pointers
 * and a lookup compares pointers. s_sem_ref is the interned copy of the
 * current state, or 0 when a command may have changed it since. A hit does
 * not copy its end state into s_sem either: s_sem_stale says s_sem still has
 * to be loaded from s_sem_ref, which walk() does before the first command
 * that reads or writes the state -- a parent list mostly calls its children
 * back to back, with only matrices in between. */
typedef struct sem_node {
    uint32_t  next;              /* word offset of the next node for this hash, 0 = none */
    uint32_t  hash;
    f3d_sem_t sem;
} sem_node_t;
#define SEM_NODE_WORDS ((uint32_t)((sizeof(sem_node_t) + 7u) / 8u * 2u))
#define SEM_POOL_HASH 1024u
static uint32_t          s_sem_pool[SEM_POOL_HASH] F3D_BIG;
static const f3d_sem_t  *s_sem_ref;
static int               s_sem_stale;
static void sem_sync(void) {
    if (s_sem_stale) { sem_copy(&s_sem, s_sem_ref); s_sem_stale = 0; }
}
static void sem_dirty(void) { sem_sync(); s_sem_ref = 0; }
static void sem_set(const f3d_sem_t *ref) { s_sem_ref = ref; s_sem_stale = 1; }
static void emitted_save(f3d_emitted_t *t) {
    memset(t, 0, sizeof *t);
    t->cc_emitted = s_cc_emitted; t->texenv_emitted = s_texenv_emitted; t->rm_emitted = s_rm_emitted;
    t->lighting_emitted = s_lighting_emitted; t->texbind_emitted = s_texbind_emitted; t->drawn = s_drawn;
}
static void emitted_load(const f3d_emitted_t *t) {
    s_cc_emitted = t->cc_emitted; s_texenv_emitted = t->texenv_emitted; s_rm_emitted = t->rm_emitted;
    s_lighting_emitted = t->lighting_emitted; s_texbind_emitted = t->texbind_emitted; s_drawn = t->drawn;
}
static void emitted_unknown(void) {
    s_cc_emitted = ~0ull; s_texenv_emitted = 0xFFFFFFFFu; s_rm_emitted = 0xFFFFFFFFu;
    s_lighting_emitted = -1; s_texbind_emitted = 0;
}

/* The platform says what is static and lends the arena; without it (the
 * host tests, other firmwares) nothing is cached. */
int f3d_dlc_static(const void *p) __attribute__((weak));
int f3d_dlc_static(const void *p) { (void)p; return 0; }
void *f3d_dlc_arena(uint32_t *bytes) __attribute__((weak));
void *f3d_dlc_arena(uint32_t *bytes) { *bytes = 0; return 0; }

typedef struct dlc_entry {
    const void *dl;
    uint32_t    next;            /* word offset of the next entry for this hash, 0 = none */
    uint32_t    nwords;          /* chunk words; 0 with DLC_NEVER */
    uint32_t    flags;
    const f3d_sem_t *in, *out;   /* interned (sem_intern) */
    f3d_emitted_t out_em;
    uint32_t    words[];         /* the chunk, GDL_ENDDL last */
} dlc_entry_t;
#define DLC_NEVER   1u           /* uncacheable: walk it */
#define DLC_HASH    4096u
#define DLC_VARIANTS 32          /* entry states kept per list before giving up on it (16 left Cool, Cool Mountain with ~4 lists a frame walked in full) */
int f3d_dlc_off;                 /* 1: walk everything (A/B measurement) */
static uint32_t  s_dlc_hash[DLC_HASH] F3D_BIG;   /* word offset of the newest entry, 0 = none */
static uint32_t *s_dlc_base;             /* this half */
static uint32_t  s_dlc_words, s_dlc_used; /* half size, used (words 0..1 are never an entry) */
static int       s_dlc_half = -1;
static unsigned  s_dlc_frame, s_dlc_switch_frame;
static int       s_dlc_translating;      /* walking into a chunk */
static int       s_dlc_abort;            /* ... which cannot be cached */
unsigned long    f3d_dlc_hits, f3d_dlc_misses, f3d_dlc_never, f3d_dlc_words_saved, f3d_dlc_switches;
unsigned long    f3d_dlc_toomany, f3d_dlc_cmps;   /* past DLC_VARIANTS; entry-state compares */

static uint32_t dlc_h(const void *dl) { uintptr_t a = (uintptr_t)dl; return (uint32_t)((a >> 3) ^ (a >> 13)) & (DLC_HASH - 1u); }
static void dlc_take_half(int half) {
    uint32_t bytes; uint32_t *base = (uint32_t *)f3d_dlc_arena(&bytes);
    s_dlc_words = (bytes / 8u) & ~1u;
    s_dlc_base = base + (uint32_t)half * s_dlc_words;
    sem_dirty();                          /* s_sem_ref points into the old half */
    s_dlc_half = half; s_dlc_used = 2u;   /* entries at even words: they hold 64-bit fields */
    memset(s_dlc_hash, 0, sizeof s_dlc_hash);
    memset(s_sem_pool, 0, sizeof s_sem_pool);
    s_dlc_switch_frame = s_dlc_frame;
}
static int dlc_enabled(void) {
    if (s_dlc_half < 0) {
        uint32_t bytes; if (!f3d_dlc_arena(&bytes) || bytes < 65536u) return 0;
        dlc_take_half(0);
    }
    return 1;
}
#define DLC_HDR_WORDS ((uint32_t)((sizeof(dlc_entry_t) + 3u) / 4u))

/* the interned copy of s_sem (s_sem_ref), added to this half if new; 0 if
 * there is no room */
static const f3d_sem_t *sem_intern(void) {
    if (s_sem_ref) return s_sem_ref;
    uint32_t hash = sem_hash(&s_sem), b = hash & (SEM_POOL_HASH - 1u);
    for (uint32_t o = s_sem_pool[b]; o; ) {
        sem_node_t *n = (sem_node_t *)(s_dlc_base + o);
        if (n->hash == hash && sem_eq(&n->sem, &s_sem)) return s_sem_ref = &n->sem;
        o = n->next;
    }
    if (s_dlc_words - s_dlc_used < SEM_NODE_WORDS) return 0;
    uint32_t off = s_dlc_used;
    sem_node_t *n = (sem_node_t *)(s_dlc_base + off);
    s_dlc_used += SEM_NODE_WORDS;
    n->next = s_sem_pool[b]; n->hash = hash;
    sem_copy(&n->sem, &s_sem);
    s_sem_pool[b] = off;
    return s_sem_ref = &n->sem;
}

/* a pointer a cached chunk may depend on: static, or the chunk is abandoned */
static void dlc_need_static(uintptr_t p) {
    if (s_dlc_translating && !f3d_dlc_static((const void *)p)) s_dlc_abort = 1;
}

static void walk(const f3d_word_t *dl, gdl_cur_t *out, int depth);

/* A gSPDisplayList to tgt: through the cache when it is static. */
static void call_dl(const f3d_word_t *tgt, gdl_cur_t *out, int depth) {
    /* (a folded translation goes into the vertices: not into a cached chunk) */
    if (s_dlc_translating || s_fold || f3d_dlc_off || !f3d_dlc_static(tgt) || !dlc_enabled()) { walk(tgt, out, depth + 1); return; }
    uint32_t h = dlc_h(tgt), variants = 0;
    const f3d_sem_t *in = 0;
    int have_in = 0;
    for (uint32_t o = s_dlc_hash[h]; o; ) {
        dlc_entry_t *e = (dlc_entry_t *)(s_dlc_base + o);
        if (e->dl == tgt) {
            if (e->flags & DLC_NEVER) { walk(tgt, out, depth + 1); return; }
            if (!have_in) { in = sem_intern(); have_in = 1; }
            f3d_dlc_cmps++;
            if (e->in == in && in) {                              /* hit */
                gdl_w(out, GDL_HDR(GDL_DL, 0));
                gdl_w(out, (uint32_t)(uintptr_t)e->words);
                sem_set(e->out);
                int drawn = s_drawn;
                emitted_load(&e->out_em);
                s_drawn |= drawn;
                f3d_dlc_hits++; f3d_dlc_words_saved += e->nwords;
                return;
            }
            variants++;
        }
        o = e->next;
    }
    /* miss: translate into a new entry at the end of this half */
    if (!have_in) in = sem_intern();
    uint32_t room = s_dlc_words - s_dlc_used;
    if (!in || variants >= DLC_VARIANTS || room < DLC_HDR_WORDS + SEM_NODE_WORDS + 4096u) {
        if (variants < DLC_VARIANTS && s_dlc_frame - s_dlc_switch_frame >= 3u) {
            dlc_take_half(s_dlc_half ^ 1);                    /* the other half, cleared */
            f3d_dlc_switches++;
            call_dl(tgt, out, depth);
            return;
        }
        if (variants >= DLC_VARIANTS) f3d_dlc_toomany++;
        walk(tgt, out, depth + 1);                            /* no room this frame: as before */
        return;
    }
    uint32_t off = s_dlc_used;
    dlc_entry_t *e = (dlc_entry_t *)(s_dlc_base + off);
    f3d_emitted_t em0;
    emitted_save(&em0);
    int drawn0 = s_drawn;
    emitted_unknown();
    s_drawn = 0;
    /* room is kept for the end state's node */
    gdl_cur_t sub = { e->words, s_dlc_base + s_dlc_words - SEM_NODE_WORDS };
    s_dlc_translating = 1; s_dlc_abort = 0;
    walk(tgt, &sub, depth + 1);
    s_dlc_translating = 0;
    gdl_w(&sub, GDL_HDR(GDL_ENDDL, 0));
    if (!s_dlc_abort && sub.p > sub.end) {
        /* did not fit in what is left of this half: put the state back, and
         * take the other half if it is free, else walk it this time */
        sem_set(in);
        emitted_load(&em0);
        s_drawn = drawn0;
        if (s_dlc_frame - s_dlc_switch_frame >= 3u && s_dlc_used > 2u) {
            dlc_take_half(s_dlc_half ^ 1);
            f3d_dlc_switches++;
            call_dl(tgt, out, depth);
        } else {
            walk(tgt, out, depth + 1);
        }
        return;
    }
    e->dl = tgt;
    e->next = s_dlc_hash[h];
    if (s_dlc_abort) {
        /* uncacheable: remember that, put the state back and walk it for real */
        e->flags = DLC_NEVER; e->nwords = 0;
        e->in = e->out = 0;
        s_dlc_used += DLC_HDR_WORDS;
        s_dlc_hash[h] = off;
        sem_set(in);
        emitted_load(&em0);
        s_drawn = drawn0;
        f3d_dlc_never++;
        walk(tgt, out, depth + 1);
        return;
    }
    e->flags = 0;
    e->nwords = (uint32_t)(sub.p - e->words);
    e->in = in;
    emitted_save(&e->out_em);
    s_drawn |= drawn0;
    s_dlc_used += (DLC_HDR_WORDS + e->nwords + 1u) & ~1u;
    s_dlc_hash[h] = off;
    e->out = sem_intern();                                    /* fits: room kept above */
    f3d_dlc_misses++;
    gdl_w(out, GDL_HDR(GDL_DL, 0));
    gdl_w(out, (uint32_t)(uintptr_t)e->words);
}

/* G_FILLRECT in fill mode (see f3d_clear_valid). Other cycle types take the
 * colour from the combiner; SM64 only uses them in its debug profiler bars. */
static void emit_fillrect(gdl_cur_t *out, uint32_t w0, uint32_t w1) {
    if (((s_othermode_h >> 20) & 3u) != 3u) return;          /* not G_CYC_FILL */
    if (s_zimg && s_cimg == s_zimg) return;                   /* the depth buffer's clear */
    int32_t xh = (int32_t)((w0 >> 12) & 0xfff) + 4, yh = (int32_t)(w0 & 0xfff) + 4;   /* inclusive */
    int32_t xl = (int32_t)((w1 >> 12) & 0xfff),     yl = (int32_t)(w1 & 0xfff);
    if (xh <= xl || yh <= yl) return;
    uint32_t p = s_fill_color >> 16;                          /* RGBA5551 */
    uint32_t r = (p >> 11) & 31u, g = (p >> 6) & 31u, b = (p >> 1) & 31u;
    r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
    if (!s_drawn && xl <= 0 && yl <= 0 && xh >= 320 * 4 && yh >= 240 * 4) {
        f3d_clear_valid = 1;
        f3d_clear_rgb = r << 16 | g << 8 | b;
        return;
    }
    /* drawn: shade only, opaque, no depth -- and the game's state put back */
    uint32_t te = s_texenv, rm = s_rm_flags;
    s_texenv = GDL_TE_RGB_SHADE | GDL_TE_A_SHADE;
    s_rm_flags = 0;
    if (s_cc_emitted >> 32) { gdl_ccolor(out, 0, 0); s_cc_emitted = 0; }   /* constants off */
    emit_texenv_if_changed(out);
    emit_rendermode_if_changed(out);
    gdl_texnorm_size(out, 0u);                                /* texturing off */
    s_texenv = te; s_rm_flags = rm;
    emit_rect_quad(out, xl, yl, xh, yh, 0, 0, 0, 0, r << 24 | g << 16 | b << 8 | 0xffu);
    emit_texnorm(out);
    s_texbind_dirty = 1;
    s_drawn = 1;
}

static void walk(const f3d_word_t *dl, gdl_cur_t *out, int depth) {
    if (depth > DL_DEPTH) {
        return;
    }
    if (!IN_SDRAM(dl)) {
        return;
    }
    for (;;) {
        if (++s_walk_iters > WALK_ITER_CAP) {
            printf("  f3d: WALK_ITER_CAP hit, dl=%p depth=%d w0=%08lx -- bail\n",
                   (void *)dl, depth, (unsigned long)dl->w0);
            return;
        }
        if (!IN_SDRAM(dl)) {
            printf("  f3d: walked off SDRAM to %p (depth %d) -- bail\n", (void *)dl, depth);
            return;
        }
        uint32_t  w0 = dl->w0;
        uintptr_t w1 = dl->w1;
        uint32_t op = (w0 >> 24) & 0xff;

        if (op != OP_DL && op != OP_ENDDL && op != OP_MTX && op != OP_POPMTX) {
            sem_sync();                              /* reads the state: see s_sem_ref */
            if (op != OP_VTX) s_sem_ref = 0;         /* ... and may change it */
        }
        if (s_dlc_translating) {
            /* what a cached chunk cannot hold: it depends on the matrix
             * stack, the viewport or the screen layout */
            if (op == OP_MTX || op == OP_POPMTX || op == OP_TEXRECT || op == OP_FILLRECT
                || op == OP_SETCIMG || op == OP_SETZIMG
                || (op == OP_MOVEMEM && (w0 & 0xff) == MV_VIEWPORT)) {
                s_dlc_abort = 1;
                return;
            }
            if (op == OP_VTX || op == OP_SETTIMG || op == OP_MOVEMEM) dlc_need_static((uintptr_t)w1);
            if (s_dlc_abort) return;
        }
        switch (op) {
        case OP_SPNOOP:
            if (w1 == F3D_TAG_FULL_VIEWPORT) {
                /* SM64's full-screen viewport: the title never sets one */
                static const int32_t vp_full[4] = { 160 << 16, 120 << 16, 511 << 14, 0 };
                for (int k = 0; k < 4; k++) { s_vp_scale[k] = vp_full[k]; s_vp_trans[k] = vp_full[k]; }
                s_vp_valid = 1;
                emit_viewport(out, vp_full, vp_full);
            }
            break;
        case OP_MTX: {
            uint32_t flags = (w0 & 0x7) ^ MTX_PUSH;
            int32_t m[16];                  /* S15.16 */
            mtx_to_fixed((const void *)w1, m);
            int target = (flags & MTX_PROJECTION) ? GDL_MTX_TARGET_PROJECTION : GDL_MTX_TARGET_MODELVIEW;
            if (flags & MTX_PROJECTION) {
                if (flags & MTX_LOAD) { for (int k = 0; k < 16; k++) s_proj[k] = m[k]; s_proj_valid = 1; }
                else if (s_proj_valid) mtx_premul_fx(s_proj, m);
                else { for (int k = 0; k < 16; k++) s_proj[k] = m[k]; s_proj_valid = 1; }
                gdl_mtx_load_fx(out, target, s_proj);
                /* w = z * m[11] + m[15]: an orthographic matrix has m[11] == 0 */
                s_proj_ortho = s_proj[11] == 0;
            } else {
                if (flags & MTX_PUSH) {
                    mv_unfold(out);                 /* the geom core pushes what it holds */
                    gdl_mtx_push(out);
                    if (s_mv_sp + 1 < F3D_MV_DEPTH) {
                        for (int k = 0; k < 16; k++) s_mv[s_mv_sp + 1][k] = s_mv[s_mv_sp][k];
                        s_mv_sp++;
                    }
                }
                int32_t *top = s_mv[s_mv_sp];
                if (flags & MTX_LOAD) { for (int k = 0; k < 16; k++) top[k] = m[k]; }
                else mtx_premul_fx(top, m);
                emit_mv(out);
            }
            break;
        }
        case OP_POPMTX: {
            int n = (int)(w1 >> 6);
            gdl_mtx_pop(out, n);
            s_emv_valid = 0; s_fold = 0;            /* the level below: not tracked */
            for (int k = n ? n : 1; k > 0 && s_mv_sp > 0; k--) s_mv_sp--;
            break;
        }

        case OP_GEOMETRYMODE:
            s_geomode = (s_geomode & (w0 & 0xffffff)) | w1;
            emit_geomode_if_changed(out);
            break;

        case OP_MOVEMEM:
            if ((w0 & 0xff) == MV_VIEWPORT) {
                const int16_t *vp = (const int16_t *)w1;
                int32_t scale[4], trans[4];             /* the Vp's 10.2 values / 4, in S15.16 */
                for (int i = 0; i < 4; i++) { scale[i] = (int32_t)vp[i] * 16384; trans[i] = (int32_t)vp[4 + i] * 16384; }
                emit_viewport(out, scale, trans);
                for (int i = 0; i < 4; i++) { s_vp_scale[i] = scale[i]; s_vp_trans[i] = trans[i]; }
                s_vp_valid = 1;
            } else if ((w0 & 0xff) == MV_LIGHT) {
                /* F3DEX2 light DMEM: G_MVO_LOOKATX=0, LOOKATY=24, L0=48, L1=72...
                 * so directional/ambient lights are ofs >= 48; ofs 0/24 are the
                 * look-at vectors for env-mapping (not handled here). */
                uint32_t ofs = ((w0 >> 8) & 0xff) * 8u;
                if (ofs == 0u || ofs == 24u) {
                    /* gSPLookAt: the X (ofs 0) and Y (24) directions G_TEXTURE_GEN maps
                     * the normals against -- a Light_t, direction at bytes 8..10 */
                    const uint8_t *L = (const uint8_t *)w1;
                    gdl_light(out, ofs ? GDL_LIGHT_LOOKAT_Y : GDL_LIGHT_LOOKAT_X, 0, s_numlights,
                              0, (int8_t)L[8], (int8_t)L[9], (int8_t)L[10]);
                } else if (ofs >= 48u) {
                    int slot = (int)((ofs - 48u) / 24u);       /* 0-based light index */
                    const uint8_t *L = (const uint8_t *)w1;    /* Light_t: col[3] pad colc[3] pad dir[3] pad */
                    uint32_t rgb = s_lightcol[slot & 7]
                                 ? s_lightcol[slot & 7]
                                 : ((uint32_t)L[0] << 16) | ((uint32_t)L[1] << 8) | L[2];
                    int is_amb = (slot >= s_numlights);        /* the light after the last directional */
                    gdl_light(out, slot & 7, is_amb, s_numlights,
                              rgb, (int8_t)L[8], (int8_t)L[9], (int8_t)L[10]);
                }
            }
            break;

        case OP_MOVEWORD: {
            /* F3DEX2 gsMoveWd: index in bits[23:16], offset in bits[15:0]. */
            uint32_t mw_idx = (w0 >> 16) & 0xff;
            uint32_t mw_ofs = w0 & 0xffff;
            if (mw_idx == MW_NUMLIGHT) {
                int n = (int)(w1 / 24u);            /* NUML(n) = n*24 */
                if (n < 1) n = 1;
                if (n > 7) n = 7;
                s_numlights = n;
            } else if (mw_idx == MW_LIGHTCOL) {
                /* offset G_MWO_a<n> = (n*24)+G_MVO_L0 for a-copy; light slot: */
                int slot = (int)((mw_ofs >= 48u ? (mw_ofs - 48u) : 0u) / 24u);
                if (slot >= 0 && slot < 8)
                    s_lightcol[slot] = ((w1 >> 8) & 0x00FFFFFFu);  /* w1 = 0xRRGGBBAA */
            }
            break;
        }

        case OP_SETCOMBINE:
            s_texenv = combine_to_texenv(w0, (uint32_t)w1);
            s_cc_w0 = w0; s_cc_w1 = (uint32_t)w1;
            break;
        case OP_SETENVCOLOR:
            s_env_rgba = (uint32_t)w1;
            break;
        case OP_SETPRIMCOLOR:
            s_prim_rgba = (uint32_t)w1;
            break;

        case OP_SETOTHERMODE_L: {
            /* F3DEX2 gSPSetOtherMode(G_SETOTHERMODE_L, sft, len, data): w0 [15:8] =
             * 32 - sft - len, [7:0] = len - 1; w1 the bits already in place. (F3D
             * used 0xB9 with sft and len stored directly -- written that way first,
             * it never matched, and every render mode was silently dropped.) SM64
             * sets the render mode per layer this way (rendering_graph_node.c). */
            uint32_t len = (w0 & 0xFFu) + 1u;
            uint32_t sft = 32u - ((w0 >> 8) & 0xFFu) - len;
            uint32_t mask = (len >= 32u ? 0xFFFFFFFFu : ((1u << len) - 1u)) << sft;
            s_othermode_l = (s_othermode_l & ~mask) | ((uint32_t)w1 & mask);
            s_rm_flags = rm_flags_of(s_othermode_l);
            break;
        }

        case OP_TEXTURE:
            /* F3DEX2 gsSPTexture: on/off is bit1 of w0. */
            s_tex_on = ((w0 >> 1) & 0x1) != 0 || (w0 & 0xff) != 0;
            /* w1 = s scale [31:16], t scale [15:0]; 0xFFFF is 1.0. The coins ask for 0x8000:
             * their vertices span 62 texels of a 32-texel texture, which only comes out as one
             * coin at half scale. Ignoring it tiled the coin 2x2. */
            if (s_tex_on) {
                uint32_t ss = ((uint32_t)w1 >> 16) & 0xffff, ts = (uint32_t)w1 & 0xffff;
                s_tex_sscale = ss >= 0xffffu ? 0x10000u : ss;
                s_tex_tscale = ts >= 0xffffu ? 0x10000u : ts;
            }
            s_texbind_dirty = 1;
            emit_texnorm(out);
            break;
        case OP_SETTILESIZE: {
            uint32_t uls = (w0 >> 12) & 0xfff, ult = w0 & 0xfff;
            uint32_t lrs = (w1 >> 12) & 0xfff, lrt = w1 & 0xfff;
            s_ts_w = (int)((lrs - uls) / 4u) + 1;
            s_ts_h = (int)((lrt - ult) / 4u) + 1;
            s_tex_render_size = (lrs > lrt ? lrs : lrt) / 4 + 1;
            s_texbind_dirty = 1;
            emit_texnorm(out);
            break;
        }
        case OP_SETTILE:
            /* w0: fmt[23:21] siz[20:19] line[17:9] tmem[8:0]
             * w1: tile[26:24] palette[23:20] cmt[19:18] maskt[17:14]
             *     shiftt[13:10] cms[9:8] masks[7:4] shifts[3:0] */
            /* Only G_TX_RENDERTILE draws: the load tile's settings (WRAP,
             * NOMASK, set just before a LOADBLOCK) must not replace the
             * render tile's. They did, and Bullet Bill's face texture --
             * CLAMP on the render tile, set before the load -- wrapped
             * instead: white texels from its far edge along the seams. */
            if (((w1 >> 24) & 0x7) == 0) {   /* G_TX_RENDERTILE */
                s_tile_fmt   = (int)((w0 >> 21) & 0x7);
                s_tile_siz   = (int)((w0 >> 19) & 0x3);
                s_tile_valid = 1;
                s_tile_cmt   = (int)((w1 >> 18) & 0x3);
                s_tile_maskt = (int)((w1 >> 14) & 0xf);
                s_tile_cms   = (int)((w1 >> 8)  & 0x3);
                s_tile_masks = (int)((w1 >> 4)  & 0xf);
                s_tile_pal   = (int)((w1 >> 20) & 0xf);
                s_texbind_dirty = 1;
            }
            break;
        case OP_SETTIMG:
            /* w0: fmt[23:21] siz[20:19] width-1[11:0] ; w1: image pointer */
            s_timg_ptr = (uintptr_t)w1;
            s_timg_fmt = (int)((w0 >> 21) & 0x7);
            s_timg_siz = (int)((w0 >> 19) & 0x3);
            s_texbind_dirty = 1;
            break;
        case OP_LOADTLUT:
            /* the SETTIMG that preceded a LOADTLUT was the palette, not the
             * texture -- stash it and let the next SETTIMG be the real image. */
            s_tlut_ptr = s_timg_ptr;
            break;

        case OP_VTX: {
            uint32_t n = (w0 >> 12) & 0xff;
            uint32_t v0 = ((w0 >> 1) & 0x7f) - n;
            const uint8_t *src = (const uint8_t *)w1;
            if (s_fold) {   /* a folded translation that would overflow a vertex: send the matrix */
                for (uint32_t i = 0; i < n && s_fold; i++) {
                    const int16_t *ob = (const int16_t *)(src + i * 16);
                    for (int k = 0; k < 3; k++) {
                        int32_t c = (int32_t)ob[k] + s_fold_d[k];
                        if (c < -32768 || c > 32767) { mv_unfold(out); break; }
                    }
                }
            }
            emit_geomode_if_changed(out);
            gdl_w(out, GDL_HDR(GDL_VTX, (n << 8) | (v0 & 0xff)));
            uint32_t *vfirst = out->p;
            if (((uintptr_t)src & 3u) == 0 && out->p + n * GDL_VTX_WORDS <= out->end) {
                /* word copies: picolibc's memcpy goes byte by byte */
                const uint32_t *sw = (const uint32_t *)src;
                uint32_t *dw = out->p;
                for (uint32_t i = 0; i < n * GDL_VTX_WORDS; i++) dw[i] = sw[i];
                out->p += n * GDL_VTX_WORDS;
            } else
            for (uint32_t i = 0; i < n; i++) {
                if (out->p + GDL_VTX_WORDS <= out->end) memcpy(out->p, src + i * 16, 16);
                out->p += GDL_VTX_WORDS;
            }
            if (s_fold && out->p <= out->end) {
                for (uint32_t i = 0; i < n; i++) {
                    int16_t *ob = (int16_t *)(vfirst + i * GDL_VTX_WORDS);
                    for (int k = 0; k < 3; k++) ob[k] = (int16_t)(ob[k] + s_fold_d[k]);
                }
            }
            break;
        }
        case OP_TRI1:
            emit_texenv_if_changed(out);
            emit_ccolor_if_changed(out);
            emit_rendermode_if_changed(out);
            emit_texbind_if_needed(out);
            gdl_tri1(out, (int)((w0 >> 17) & 0x7f), (int)((w0 >> 9) & 0x7f), (int)((w0 >> 1) & 0x7f));
            s_drawn = 1;
            break;
        case OP_TRI2:
            emit_texenv_if_changed(out);
            emit_ccolor_if_changed(out);
            emit_rendermode_if_changed(out);
            emit_texbind_if_needed(out);
            gdl_tri2(out,
                     (int)((w0 >> 17) & 0x7f), (int)((w0 >> 9) & 0x7f), (int)((w0 >> 1) & 0x7f),
                     (int)((w1 >> 17) & 0x7f), (int)((w1 >> 9) & 0x7f), (int)((w1 >> 1) & 0x7f));
            s_drawn = 1;
            break;

        case OP_SETOTHERMODE_H: {
            uint32_t len = (w0 & 0xffu) + 1u, sft = 32u - ((w0 >> 8) & 0xffu) - len;
            uint32_t mask = (len >= 32u ? 0xffffffffu : ((1u << len) - 1u)) << sft;
            s_othermode_h = (s_othermode_h & ~mask) | ((uint32_t)w1 & mask);
            break;
        }
        case OP_SETFILLCOLOR:
            s_fill_color = (uint32_t)w1;
            break;
        case OP_SETCIMG:
            s_cimg = (uintptr_t)w1;
            break;
        case OP_SETZIMG:
            s_zimg = (uintptr_t)w1;
            break;
        case OP_FILLRECT:
            emit_fillrect(out, w0, (uint32_t)w1);
            break;
        case OP_TEXRECT:
            s_drawn = 1;
            emit_texrect(out, w0, (uint32_t)w1, (uint32_t)dl[1].w1, (uint32_t)dl[2].w1);
            dl += 2;   /* its G_RDPHALF_1 and G_RDPHALF_2 */
            break;

        case OP_DL: {
            int is_branch = ((w0 >> 16) & 0xff) != 0;
            const f3d_word_t *tgt = (const f3d_word_t *)w1;
            dlc_need_static((uintptr_t)tgt);
            if (is_branch) { dl = tgt; continue; }
            call_dl(tgt, out, depth);
            break;
        }
        case OP_ENDDL:
            return;

        default:
            break;  /* every unhandled opcode here is 1 Gfx word */
        }
        dl++;
    }
}

void f3d_emit_display_list(const f3d_word_t *dl, gdl_cur_t *out) {
    s_dlc_frame++;
    s_walk_iters = 0;
    s_drawn = 0; f3d_clear_valid = 0;
    walk(dl, out, 0);
}
