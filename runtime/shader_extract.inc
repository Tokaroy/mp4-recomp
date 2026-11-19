/* ============================================================ shaders extracted from the game files (--extract-shaders)
   The launcher's EXTRACT SHADERS button (Advanced > Shaders). The game program starts with its window hidden and no game
   running, reads every .pak / .arc file of the player's own RomFS, finds the NVN shader programs in their zstd frames and
   translates each one to GLSL the way the pre-build from GPU memory does when an area loads them (the same variants under
   the same keys: mem_prebuild_step itself, told not to compile). MaterialArchive.arc holds the boot shaders; the area
   .pak files keep theirs in frames nested inside other frames (an outer frame stores the inner ones as raw blocks).
   About 68,000 programs in all, of which a session uses a few thousand.
   The translations go to shader_cache/extracted_v<translator>.bin: each entry zstd-compressed, then a sorted index. The
   game reads only the index when it starts (16 bytes a translation) and an entry the first time it asks for that shader,
   so the store's size on disk doesn't become memory. Nothing is compiled here: the driver still compiles a shader in the
   background when the game loads it, as before. A store from another translator version is deleted at start.
   Linux only (the system's libzstd, loaded when needed) */
extern volatile int g_xs_done, g_xs_rc;   /* main.c, with g_xs */
#define XS_PATH "shader_cache/extracted_v" TR_STR(TR_VER) ".bin"
#define XS_MAGIC 0x31545853u   /* "SXT1" */
typedef struct { u64 key, off; } XsIdx;   /* off: the entry's file offset; bit 63 once it has been read */
typedef struct { u32 magic, version, si_size, count; u64 index_off, raw_bytes; } XsHead;
#ifndef _WIN32
#include <dlfcn.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
typedef struct { const void* src; size_t size, pos; } ZIn; typedef struct { void* dst; size_t size, pos; } ZOut;
static struct { void* (*dcreate)(void); size_t (*dstream)(void*,ZOut*,ZIn*); size_t (*dreset)(void*,int); unsigned (*iserr)(size_t);
  size_t (*decompress)(void*,size_t,const void*,size_t); size_t (*compress)(void*,size_t,const void*,size_t,int); size_t (*bound)(size_t); } Z;
static int xs_zstd(void){ static int ok=-1; if(ok>=0) return ok; ok=0;
  void* m=dlopen("libzstd.so.1",RTLD_NOW|RTLD_LOCAL); if(!m) m=dlopen("libzstd.so",RTLD_NOW|RTLD_LOCAL); if(!m) return 0;
  *(void**)&Z.dcreate=dlsym(m,"ZSTD_createDCtx"); *(void**)&Z.dstream=dlsym(m,"ZSTD_decompressStream"); *(void**)&Z.dreset=dlsym(m,"ZSTD_DCtx_reset");
  *(void**)&Z.iserr=dlsym(m,"ZSTD_isError"); *(void**)&Z.decompress=dlsym(m,"ZSTD_decompress"); *(void**)&Z.compress=dlsym(m,"ZSTD_compress");
  *(void**)&Z.bound=dlsym(m,"ZSTD_compressBound");
  ok= Z.dcreate && Z.dstream && Z.dreset && Z.iserr && Z.decompress && Z.compress && Z.bound; return ok; }

/* ---- the game: the index at start, an entry when first asked for */
static XsIdx* g_xsi; static u32 g_xsi_n; static int g_xs_fd=-1; static PMutex g_xs_m;
static void xs_open(void){
  if(g_xs) return;   /* extracting: the store being written isn't read */
  { PDir d; if(!plat_dir_open(&d,"shader_cache")){ while(plat_dir_next(&d)){   /* stores of other translator versions: no game reads them again */
        if(!strncmp(d.name,"extracted_v",11) && strcmp(d.name,XS_PATH+13)){ char p[600]; snprintf(p,sizeof p,"shader_cache/%s",d.name);
          if(!remove(p)) LOG("shaders: removed %s (from another shader translator version)\n",p); } }
      plat_dir_close(&d); } }
  int fd=open(XS_PATH,O_RDONLY|O_CLOEXEC); if(fd<0) return;
  XsHead h; u64 sz=(u64)lseek(fd,0,SEEK_END);
  if(pread(fd,&h,sizeof h,0)!=(ssize_t)sizeof h || h.magic!=XS_MAGIC || h.version!=TR_VERSION || h.si_size!=sizeof(ShaderInfo) || !h.count
     || h.index_off+(u64)h.count*sizeof(XsIdx)!=sz || !xs_zstd()){ close(fd); LOG("shaders: %s is unusable (another version, unfinished or no libzstd): extract the shaders again\n",XS_PATH); return; }
  XsIdx* x=malloc((size_t)h.count*sizeof *x);
  if(!x || pread(fd,x,(size_t)h.count*sizeof *x,(off_t)h.index_off)!=(ssize_t)(h.count*sizeof *x)){ free(x); close(fd); return; }
  pm_init(&g_xs_m); g_xsi=x; g_xsi_n=h.count; g_xs_fd=fd;
  LOG("shaders: %u translations extracted from the game files available (%s, %.0f MB; read as the game needs them)\n",h.count,XS_PATH,sz/1048576.0); }
static Translated* xs_lookup(u64 key){
  if(!g_xsi_n) return NULL;
  u32 lo=0, hi=g_xsi_n; while(lo<hi){ u32 m=(lo+hi)/2; if(g_xsi[m].key<key) lo=m+1; else hi=m; }
  if(lo>=g_xsi_n || g_xsi[lo].key!=key) return NULL;
  pm_lock(&g_xs_m); Translated* t=NULL; u64 off=g_xsi[lo].off;
  if(!(off>>63)){ g_xsi[lo].off|=1ull<<63; u32 hd2[2];   /* compressed size, raw size */
    if(pread(g_xs_fd,hd2,8,(off_t)off)==8 && hd2[0] && hd2[0]<(16u<<20) && hd2[1]>=20+8+sizeof(ShaderInfo) && hd2[1]<(16u<<20)){
      u8* c=malloc(hd2[0]); u8* r=malloc(hd2[1]+1);
      if(c && r && pread(g_xs_fd,c,hd2[0],(off_t)(off+8))==(ssize_t)hd2[0] && Z.decompress(r,hd2[1],c,hd2[0])==hd2[1]){
        u32 hd[5]; u64 k; memcpy(hd,r,20); memcpy(&k,r+20,8); u8* q=r+28;
        if(hd[0]==0x544c4753 && hd[1]==TR_VERSION && hd[2]==sizeof(ShaderInfo) && hd[4]<=128 && k==key && 28+sizeof(ShaderInfo)+hd[4]*sizeof(TransDep)+hd[3]==hd2[1]){
          t=calloc(1,sizeof *t); t->key=key; memcpy(&t->si,q,sizeof t->si); q+=sizeof t->si; t->ndep=hd[4]; memcpy(t->dep,q,t->ndep*sizeof(TransDep)); q+=t->ndep*sizeof(TransDep);
          t->src=malloc((size_t)hd[3]+1); memcpy(t->src,q,hd[3]); t->src[hd[3]]=0; translated_put(t); g_xs_loaded++; } }
      free(c); free(r); } }
  pm_unlock(&g_xs_m); return t; }

/* ---- extracting: the writer */
static FILE* g_xw; static XsIdx* g_xwi; static u32 g_xwi_n, g_xwi_cap; static u64 g_xw_off, g_xw_raw; static int g_xw_err;
static void xs_store(u64 key, const ShaderInfo* si, const char* src){
  if(!g_xw || g_xw_err) return;
  u32 n=(u32)strlen(src), hd[5]={0x544c4753,TR_VERSION,sizeof(ShaderInfo),n,trans_ndep};
  size_t raw=sizeof hd+8+sizeof *si+trans_ndep*sizeof(TransDep)+n; u8* d=malloc(raw); u8* q=d;
  memcpy(q,hd,sizeof hd); q+=sizeof hd; memcpy(q,&key,8); q+=8; memcpy(q,si,sizeof *si); q+=sizeof *si; memcpy(q,trans_dep,trans_ndep*sizeof(TransDep)); q+=trans_ndep*sizeof(TransDep); memcpy(q,src,n);
  size_t cb=Z.bound(raw); u8* c=malloc(cb+8); size_t cn=Z.compress(c+8,cb,d,raw,6);
  if(!Z.iserr(cn)){ u32 h2[2]={(u32)cn,(u32)raw}; memcpy(c,h2,8);
    if(fwrite(c,1,cn+8,g_xw)!=cn+8) g_xw_err=errno? errno : EIO;
    else { if(g_xwi_n==g_xwi_cap){ g_xwi_cap= g_xwi_cap? g_xwi_cap*2 : 4096; g_xwi=realloc(g_xwi,g_xwi_cap*sizeof *g_xwi); }
      g_xwi[g_xwi_n++]=(XsIdx){key,g_xw_off}; g_xw_off+=cn+8; g_xw_raw+=raw; } }
  free(c); free(d); }
static int xs_cmp(const void* a, const void* b){ u64 x=((const XsIdx*)a)->key, y=((const XsIdx*)b)->key; return x<y? -1 : x>y; }

/* ---- extracting: the scan */
static u64* g_xs_seen; static u32 g_xs_nseen, g_xs_files, g_xs_frames, g_xs_vs, g_xs_fs; static u64 g_xs_bytes, g_xs_total, g_xs_t0, g_xs_lastp;
static void xs_progress(u64 at){   /* at most 4 a second: percent of the bytes read */
  u64 now=plat_ns(); if(now-g_xs_lastp<250000000ull) return; g_xs_lastp=now;
  double f= g_xs_total? (double)(g_xs_bytes+at)/g_xs_total : 0; if(f>0.999) f=0.999;
  printf("@@P %.1f Extracting shaders|%u shaders found, %u translated  \xC2\xB7  %.1f of %.1f GB read\n",100.0*f,g_xs_vs+g_xs_fs,g_mp_built,(g_xs_bytes+at)/1e9,g_xs_total/1e9); fflush(stdout); }
/* the programs in a decompressed frame: each new one into the pre-build's queue, then translated right away (only the
   program and 4 KB after it are copied: the queue holds one or two at a time) */
static void xs_programs(const u8* b, size_t n){
  for(size_t i=0;i+0x30+0x80<=n;i+=4){ if(memcmp(b+i,"\x78\x56\x34\x12",4)) continue;
    const u8* code=b+i+0x30; u32 w0; memcpy(&w0,code,4); if(((w0>>5)&31)!=3 || ((w0&31)!=1 && (w0&31)!=2)) continue;
    u32 avail= n-(i+0x30)>0x10000? 0x10000 : (u32)(n-(i+0x30)), len=0; int ty=0; if(!shader_program_check(code,avail,&len,&ty)) continue;
    int stage= ty==1? ST_VS : ty==5? ST_FS : -1; if(stage<0) continue;
    u32 hl; u64 hh=shader_hash(code,avail,&hl); u64 k=(hh^(u64)(stage+1))|1; u32 s=(u32)(k^(k>>31))&0x1FFFF;
    while(g_xs_seen[s] && g_xs_seen[s]!=k) s=(s+1)&0x1FFFF;
    if(g_xs_seen[s] || g_xs_nseen>=120000) continue; g_xs_seen[s]=k; g_xs_nseen++; if(stage==ST_VS) g_xs_vs++; else g_xs_fs++;
    u32 keep= hl+0x1000<avail? hl+0x1000 : avail; u8* copy=malloc(keep); if(!copy) continue; memcpy(copy,code,keep);
    pm_lock(&g_mp_m); if(g_mp_n==g_mp_cap){ g_mp_cap= g_mp_cap? g_mp_cap*2 : 1024; g_mp=realloc(g_mp,g_mp_cap*sizeof *g_mp); }
    g_mp[g_mp_n++]=(MemProg){copy,keep,stage,hh,plat_ns()}; pm_unlock(&g_mp_m);
    for(;;){ pm_lock(&g_mp_m); int more=g_mp_next<g_mp_n; pm_unlock(&g_mp_m); if(!more) break; mem_prebuild_step(); } } }
/* every zstd frame in a buffer: decompressed (up to 64 MB: bigger ones are textures) and searched for programs and for
   frames inside it, two levels down. The search goes on right after each frame's magic, not after its end: an outer
   frame stores inner frames as raw blocks */
static void xs_frames(const u8* b, u64 size, int depth){
  static void* dc[3]; static u8* out[3]; static size_t cap[3];
  if(!dc[depth]){ dc[depth]=Z.dcreate(); cap[depth]=1u<<20; out[depth]=malloc(cap[depth]); }
  u64 dropped=0;
  for(u64 at=0;at+4<=size;){
    /* in the files, searched 64 MB at a time and the pages behind given back: long stretches without a frame (textures)
       otherwise piled up as this process's memory (3.5 GB) */
    u64 win= depth? size-at : (size-at<(64ull<<20)? size-at : 64ull<<20);
    const u8* p=memmem(b+at,win,"\x28\xb5\x2f\xfd",4);
    if(!p){ if(depth || at+win>=size) break; at+=win-3; }
    else at=(u64)(p-b);
    if(!depth){ xs_progress(at);
      u64 upto=at&~((64ull<<20)-1); if(upto>dropped+(128ull<<20)){ madvise((void*)(b+dropped),upto-dropped,MADV_DONTNEED); dropped=upto; } }
    if(!p) continue;
    Z.dreset(dc[depth],1); ZIn in={b+at,size-at,0}; ZOut o={out[depth],cap[depth],0}; int done=0;
    for(;;){ size_t r=Z.dstream(dc[depth],&o,&in); if(Z.iserr(r)) break; if(r==0){ done=1; break; }
      if(o.pos==o.size){ if(cap[depth]>=(64u<<20)) break; cap[depth]*=2; out[depth]=realloc(out[depth],cap[depth]); o.dst=out[depth]; o.size=cap[depth]; }
      else if(in.pos==in.size) break; }
    if(done){ g_xs_frames++; xs_programs(out[depth],o.pos); if(depth<2) xs_frames(out[depth],o.pos,depth+1); }
    at+=4; } }
static void xs_walk(const char* dir, int count){ PDir d; if(plat_dir_open(&d,dir)) return;
  while(plat_dir_next(&d)){ if(!strcmp(d.name,".") || !strcmp(d.name,"..")) continue; char p[2048]; snprintf(p,sizeof p,"%s/%s",dir,d.name);
    if(d.is_dir){ xs_walk(p,count); continue; }
    size_t l=strlen(d.name); if(l<4 || (strcmp(d.name+l-4,".pak") && strcmp(d.name+l-4,".arc"))) continue;
    if(count){ g_xs_total+=d.size; continue; }
    int fd=open(p,O_RDONLY|O_CLOEXEC); if(fd<0) continue; const u8* b= d.size? mmap(NULL,d.size,PROT_READ,MAP_PRIVATE,fd,0) : MAP_FAILED; close(fd);
    if(b!=MAP_FAILED){ madvise((void*)b,d.size,MADV_SEQUENTIAL); g_xs_files++; xs_frames(b,d.size,0); munmap((void*)b,d.size); }
    g_xs_bytes+=d.size; }
  plat_dir_close(&d); }
#else
static void xs_open(void){}
static Translated* xs_lookup(u64 key){ (void)key; return NULL; }
static void xs_store(u64 key, const ShaderInfo* si, const char* src){ (void)key; (void)si; (void)src; }
#endif
static void xs_finish(int rc, const char* msg){ if(msg){ printf("%s\n",msg); LOG("shaders: extract: %s\n",msg+(msg[0]=='@'? 4 : 0)); }
  fflush(stdout); log_flush(); g_xs_rc=rc; __atomic_store_n(&g_xs_done,1,__ATOMIC_RELEASE); for(;;) plat_sleep_ns(100000000); }
static void xs_run(void){
#ifdef _WIN32
  xs_finish(3,"@@E Extracting the shaders works on Linux only for now.");
#else
  if(!xs_zstd()) xs_finish(3,"@@E Extracting the shaders needs libzstd (the zstd package), which this system lacks.");
  if(g_mp_on<0) mem_prebuild_step();
  if(g_mp_on<=0) xs_finish(3,"@@E The shaders can't be extracted with these settings: it needs OpenGL with \"Build shaders before playing\" on (Advanced) and a driver that can store compiled shaders.");
  extern char g_romfs_dir_override[1024]; char dir[1200];
  if(g_romfs_dir_override[0]) snprintf(dir,sizeof dir,"%s",g_romfs_dir_override); else snprintf(dir,sizeof dir,"%sromfs",g_datadir);
  plat_mkdir("shader_cache");
  const char* tmp="shader_cache/extracted.tmp"; g_xw=fopen(tmp,"wb");
  if(!g_xw) xs_finish(4,"@@E Could not write to the shader_cache folder.");
  { XsHead z; memset(&z,0,sizeof z); fwrite(&z,sizeof z,1,g_xw); g_xw_off=sizeof z; }
  { FILE* f=fopen("/proc/self/statm","r"); unsigned long sz=0, rss=0; if(f){ if(fscanf(f,"%lu %lu",&sz,&rss)!=2) rss=0; fclose(f); }
    LOG("shaders: extract: %.0f MB of memory in use before reading the files (the game program and the graphics driver)\n",rss*4096.0/1048576.0); }
  g_xs_t0=plat_ns(); g_xs_seen=calloc(1u<<17,8);
  printf("@@P 0 Extracting shaders|looking through the game files\n"); fflush(stdout);
  xs_walk(dir,1); LOG("shaders: extract: %.1f GB of .pak / .arc files in %s\n",g_xs_total/1e9,dir);
  if(!g_xs_total){ fclose(g_xw); remove(tmp); xs_finish(4,"@@E No game files (.pak / .arc) were found in the game's romfs folder."); }
  xs_walk(dir,0);
  u32 found=g_xs_vs+g_xs_fs;
  LOG("shaders: extract: %u programs (%u vertex, %u pixel) in %u zstd frames of %u files, %.1f s; %u translations, %u not translated\n",
      found,g_xs_vs,g_xs_fs,g_xs_frames,g_xs_files,(plat_ns()-g_xs_t0)/1e9,g_xwi_n,g_mp_failed);
  if(!g_xwi_n){ fclose(g_xw); remove(tmp); xs_finish(4,"@@E No shaders were found in the game files."); }
  /* the index, sorted (a key stored twice: the first one), then the header */
  qsort(g_xwi,g_xwi_n,sizeof *g_xwi,xs_cmp); u32 n=0; for(u32 i=0;i<g_xwi_n;i++) if(!n || g_xwi[n-1].key!=g_xwi[i].key) g_xwi[n++]=g_xwi[i];
  XsHead h={XS_MAGIC,TR_VERSION,sizeof(ShaderInfo),n,g_xw_off,g_xw_raw};
  if(fwrite(g_xwi,sizeof *g_xwi,n,g_xw)!=n || fseek(g_xw,0,SEEK_SET) || fwrite(&h,sizeof h,1,g_xw)!=1 || fflush(g_xw) || fsync(fileno(g_xw))) g_xw_err= errno? errno : EIO;
  fclose(g_xw); g_xw=NULL;
  if(g_xw_err){ remove(tmp); char m[300]; snprintf(m,sizeof m,"@@E Could not write the extracted shaders (%s): is the disk full?",strerror(g_xw_err)); xs_finish(4,m); }
  if(rename(tmp,XS_PATH)){ remove(tmp); xs_finish(4,"@@E Could not save the extracted shaders in the shader_cache folder."); }
  double mb=(g_xw_off+n*sizeof(XsIdx))/1048576.0, secs=(plat_ns()-g_xs_t0)/1e9;
  { FILE* f=fopen("shader_cache/extracted.txt","w"); time_t now=time(NULL); char ds[64]; strftime(ds,sizeof ds,"%Y-%m-%d %H:%M",localtime(&now));
    if(f){ fprintf(f,"file %s\nprograms %u\nvertex %u\npixel %u\ntranslations %u\nnot_translated %u\nmegabytes %.0f\nseconds %.0f\ndate %s\n",XS_PATH+13,found,g_xs_vs,g_xs_fs,n,g_mp_failed,mb,secs,ds); fclose(f); } }
  { struct rusage ru; getrusage(RUSAGE_SELF,&ru);
    LOG("shaders: extract: %u translations saved to %s (%.0f MB, %.0f MB uncompressed) in %.0f s, at most %.0f MB of memory used\n",n,XS_PATH,mb,g_xw_raw/1048576.0,secs,ru.ru_maxrss/1024.0); }
  printf("@@P 100 Done|%u shaders extracted, %u translations saved (%.0f MB)\n",found,n,mb); printf("@@D\n");
  xs_finish(0,NULL);
#endif
}
