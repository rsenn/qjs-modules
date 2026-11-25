/* diet-compat.h: what dietlibc lacks: byte order macros, float math, NAN, locale_t.
 * the float functions are macros, a static inline would clash with a declaration in math.h.
 * included with -include into third-party code built for dietlibc.
 * rule: each float result is exact, a double call rounded back to float. */
#ifndef __ASSEMBLER__ /* -include also reaches the .S files */
#include <endian.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* dietlibc's endian.h has no htobe32() and friends */
#if !defined(be32toh) && __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe16(x) __builtin_bswap16(x)
#define htobe32(x) __builtin_bswap32(x)
#define htobe64(x) __builtin_bswap64(x)
#define be16toh(x) __builtin_bswap16(x)
#define be32toh(x) __builtin_bswap32(x)
#define be64toh(x) __builtin_bswap64(x)
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)
#endif

/* declared in dietlibc math.h but missing from libm.a: copysign, truncf */
static inline double
diet_copysign(double x, double y) {
  uint64_t a, b;

  memcpy(&a, &x, 8);
  memcpy(&b, &y, 8);
  a = (a & ~(1ULL << 63)) | (b & (1ULL << 63));
  memcpy(&x, &a, 8);

  return x;
}

#define copysign(x, y) diet_copysign(x, y)
#define truncf(x) ((float)trunc(x))

#define copysignf(x, y) ((float)copysign((x), (y)))
#define ceilf(x) ((float)ceil(x))
#define floorf(x) ((float)floor(x))
#define sqrtf(x) ((float)sqrt(x))
#define rintf(x) ((float)rint(x))
#define fabsf(x) ((float)fabs(x))

/* dietlibc's math.h has neither */
#ifndef NAN
#define NAN __builtin_nanf("")
#endif
#ifndef INFINITY
#define INFINITY __builtin_inff()
#endif

/* no locales in dietlibc: strtod_l() is strtod() */
typedef int locale_t;
#define LC_ALL_MASK 0
#define newlocale(mask, name, base) ((locale_t)0)
#define strtod_l(s, end, loc) strtod((s), (end))

/* u_char and u_int come with _BSD_SOURCE only, which also defines ulong */
typedef unsigned char u_char;
typedef unsigned int u_int;

#endif /* __ASSEMBLER__ */
