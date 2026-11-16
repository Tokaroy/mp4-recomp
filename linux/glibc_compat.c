/* The AppImage's runtime on older distributions: the Bootlin GCC links against glibc 2.41, but an AppImage should run on
   the oldest Ubuntu LTS still supported (22.04, glibc 2.35). Only a few functions get newer symbol versions there:
   - fmod/fmodf: new versions in glibc 2.38 (the same results; only the old SVID error handling went away)
   - the __isoc23_ strtol and scanf families: what _GNU_SOURCE (and C++, which always defines it) calls since 2.38;
     they differ only in also reading a 0b binary prefix, which nothing here parses
   - arc4random (2.36): libstdc++'s std::random_device, linked statically
   make_appimage.sh links with -Wl,--wrap for each of them, so every call comes here and goes to the old one (or to
   getrandom, glibc 2.25). Built without _GNU_SOURCE, so the strtol / scanf below are the plain ISO C ones. */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>

double swr_fmod_old(double, double);
float swr_fmodf_old(float, float);
__asm__(".symver swr_fmod_old,fmod@GLIBC_2.2.5");
__asm__(".symver swr_fmodf_old,fmodf@GLIBC_2.2.5");
double __wrap_fmod(double x, double y){ return swr_fmod_old(x, y); }
float __wrap_fmodf(float x, float y){ return swr_fmodf_old(x, y); }

long __wrap___isoc23_strtol(const char* s, char** e, int b){ return strtol(s, e, b); }
long long __wrap___isoc23_strtoll(const char* s, char** e, int b){ return strtoll(s, e, b); }
unsigned long __wrap___isoc23_strtoul(const char* s, char** e, int b){ return strtoul(s, e, b); }
unsigned long long __wrap___isoc23_strtoull(const char* s, char** e, int b){ return strtoull(s, e, b); }
int __wrap___isoc23_sscanf(const char* s, const char* f, ...){ va_list a; va_start(a, f); int r = vsscanf(s, f, a); va_end(a); return r; }
int __wrap___isoc23_fscanf(FILE* s, const char* f, ...){ va_list a; va_start(a, f); int r = vfscanf(s, f, a); va_end(a); return r; }

uint32_t __wrap_arc4random(void){ uint32_t v = 0; size_t got = 0;
  while(got < sizeof v){ ssize_t n = getrandom((char*)&v + got, sizeof v - got, 0); if(n > 0) got += (size_t)n; else if(errno != EINTR) break; }
  return v; }
