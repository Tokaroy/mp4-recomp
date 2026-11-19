/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs. */
/* Maxwell (GM20B) shader -> GLSL 4.30 translator */
#ifndef SWR_SHADER_H
#define SWR_SHADER_H
#include "cpu.h"
enum { ST_VS=0, ST_TCS=1, ST_TES=2, ST_GS=3, ST_FS=4, ST_CS=5 };
#define SH_MAXTEX 32
typedef struct {
  u32 cbuf_off;        /* handle offset in the texture cbuf, in words */
  u8 type;             /* 0 1D 1 2D 2 3D 3 CUBE */
  u8 array, shadow, fetch_only, is_int;
  u8 cmask;            /* channels the shader reads (gpu_gl.c: depth-stencil textures read for their stencil) */
  u8 bindless; u8 bl_cbuf; u8 bl2; u8 bl2_cbuf;   /* bl2: handle = read1 | read2 (separate tex/sampler handles) */
  u32 bl2_off;          /* word offset of read2 */
} ShTex;
typedef struct {
  int stage;
  u32 sph[20];
  u32 cbuf_mask;                 /* cbufs used */
  u32 cbuf_indirect_mask;
  u32 cbuf_max[18];              /* bytes used per cbuf, so we upload less */
  ShTex tex[SH_MAXTEX]; int ntex;
  u32 in_mask, out_mask;         /* generic attribs 0..31 */
  u32 attr_int_mask;             /* key: integer VS attribs */
  u32 attr_sint_mask;
  u32 fs_flat_mask;              /* key */
  int writes_depth, uses_position_in, uses_clip_mask, writes_layer, writes_point_size, uses_front_face;
  u32 frag_rt_mask;
  int uses_global, uses_local, local_size;
  u32 gbuf_cbuf_off[16]; int ngbuf;  /* global buffers found: where their 64-bit address is (constant buffer offset) */
  u8 gbuf_cbuf_idx[16];          /* ... in which constant buffer */
  u8 gbuf_sized[16];             /* 1: NVN's SSBO table, {address, size}; 0: a bare pointer (the game's), size unknown */
  int unsupported;               /* count of instructions we couldn't translate */
  char first_unsupported[64];
  u32 cs_local[3]; int shared_size;
  u32 gbuf_amask;                /* gbufs updated with atomics (counters the CPU reads: gpu_gl.c gw_sync) */
  u32 gbuf_wmask;                /* gbufs written (compute) */
  struct { u32 cbuf_off; u8 type, bindless, bl_cbuf, pad; } img[8]; int nimg;   /* SUST targets (compute) */
  u32 sutex_mask;                /* SULD.D reads, these need their format class in swr_tc */
  u32 tex_int_mask, tex_sint_mask, rt_int_mask, rt_sint_mask;  /* FS key: integer textures / outputs */
} ShaderInfo;
/* malloc'd GLSL, or NULL */
char* shader_to_glsl(const u8* code, u32 max_bytes, int stage, ShaderInfo* si);
extern int (*g_shader_cb_read)(u32 idx, u32 off, u32* out);
u64 shader_hash(const u8* code, u32 max_bytes, u32* out_len);
/* global buffer (SSBO) binding of buffer i of a draw stage (slot 0 vertex, 1 pixel, 2 geometry): the first 8 keep
   the original layout, buffers 8-15 sit above all of those (binding 24+), when the driver has 48 bindings and 16 per
   stage (g_gbuf_max 16; gpu_gl.c sets it). Metroid Prime 4's skinning/effect vertex shaders write to up to 11
   buffers: with 8 the stores to the rest were dropped */
extern int g_gbuf_max;
static inline int swr_gb_bind(int slot, int i){ return i<8? slot*8+i : 24+slot*8+(i-8); }
/* Per-instance vertex attributes the vertex shader reads itself (gpu_gl.c turns it on for radeonsi). radeonsi compiles a
   vertex shader with any instanced attribute as a separate "monolithic" variant at its first draw, on the thread that
   draws, and doesn't keep it in its disk cache: 25-40 ms per variant, every time the game starts, and again for each
   pixel shader or cull mode the vertex shader is later paired with. Prime 4 draws much of its scenery instanced, so it
   was most of the stutter (area loads and the hitches after). Read from a storage buffer instead, the driver builds
   these shaders like any other: in the background, once, from its cache after that.
   g_ia_bind: storage-buffer binding of the first such attribute (0: off); g_vs_ssbo_max: storage blocks a vertex shader
   may use. g_vs_ia_req: the instanced attributes of the draw (the key; set around shader_to_glsl). swr_ia_mask: the ones
   a vertex shader with these inputs and global buffers reads itself, k-th attribute at binding g_ia_bind+k (the rest
   keep a hardware divisor) */
extern int g_ia_bind, g_vs_ssbo_max; extern __thread u32 g_vs_ia_req; extern const char g_ia_glsl[], g_ia_fetch[];
static inline u32 swr_ia_mask(u32 req, u32 in_mask, int ngbuf){
  if(!g_ia_bind) return 0;
  u32 f=req&in_mask&0xFFFFu, o=0; int avail=g_vs_ssbo_max-ngbuf;
  for(int i=0;i<16 && avail>0;i++) if((f>>i)&1){ o|=1u<<i; avail--; }
  return o; }
#endif
