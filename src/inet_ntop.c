#if !defined(HAVE_INET_NTOP) && !defined(__MSYS__) && !defined(__CYGWIN__) && !defined(__MINGW32__)
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#define socklen_t int
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

const char*
inet_ntop(int af, const void* a0, char* s, socklen_t l) {
  const unsigned char* a = a0;
  int i, j, max, best;
  char buf[100];

  switch(af) {
    case AF_INET:
      if(snprintf(s, l, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]) < (int)l)
        return s;

      break;
    case AF_INET6:
      if(memcmp(a, "\0\0\0\0\0\0\0\0\0\0\377\377", 12))
        snprintf(buf,
                 sizeof buf,
                 "%x:%x:%x:%x:%x:%x:%x:%x",
                 256 * a[0] + a[1],
                 256 * a[2] + a[3],
                 256 * a[4] + a[5],
                 256 * a[6] + a[7],
                 256 * a[8] + a[9],
                 256 * a[10] + a[11],
                 256 * a[12] + a[13],
                 256 * a[14] + a[15]);
      else
        snprintf(buf,
                 sizeof buf,
                 "%x:%x:%x:%x:%x:%x:%d.%d.%d.%d",
                 256 * a[0] + a[1],
                 256 * a[2] + a[3],
                 256 * a[4] + a[5],
                 256 * a[6] + a[7],
                 256 * a[8] + a[9],
                 256 * a[10] + a[11],
                 a[12],
                 a[13],
                 a[14],
                 a[15]);
      /* Replace longest /(^0|:)[:0]{2,}/ with "::" */
      for(i = best = 0, max = 2; buf[i]; i++) {
        if(i && buf[i] != ':')
          continue;

        j = strspn(buf + i, ":0");

        if(j > max)
          best = i, max = j;
      }

      if(max > 3) {
        buf[best] = buf[best + 1] = ':';
        memmove(buf + best + 2, buf + best + max, i - best - max + 1);
      }

      if(strlen(buf) < l) {
        strcpy(s, buf);
        return s;
      }

      break;
    default: errno = EAFNOSUPPORT; return 0;
  }

  errno = ENOSPC;
  return 0;
}
#endif /* !defined(HAVE_INET_NTOP) && !defined(__MSYS__) */
