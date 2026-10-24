#ifndef VTCONSOLE_H
#define VTCONSOLE_H

#ifdef _WIN32
#include <windows.h>
#include "vtparse.h"

/**
 * \defgroup vtconsole vtconsole: VT sequences on a Win32 console
 *
 * for a console without VT processing:
 *   - output: sequences parsed by vtparse, carried out by console calls
 *   - input: key events turned into the bytes a VT terminal sends
 * @{
 */

typedef struct vt_console {
  VtParser parser;
  HANDLE handle;
  WORD default_attr; /* attributes at vtconsole_init(), what SGR 0 restores */
  int fg, bg;        /* ANSI color 0-15, -1 for the default */
  unsigned bold : 1, underline : 1, reverse : 1, failed : 1;
  COORD saved;  /* cursor stored by ESC 7 or CSI s, window relative */
  char tail[4]; /* incomplete UTF-8 sequence at the end of a write */
  int ntail;
} VtConsole;

/* vtconsole_init: binds vc to the console output handle h.
 *
 * the attributes h has now become the default colors. never fails.
 */
void vtconsole_init(VtConsole* vc, HANDLE h);

/* vtconsole_write: carries out len bytes of VT output on the console.
 *
 * understood: text (UTF-8), CR LF BS HT BEL, ESC 7 8 c,
 * CSI A B C D E F G H f d J K X m s u, CSI ?25 h l.
 * any other sequence is dropped.
 *
 *   returns BOOL  FALSE when a console call failed
 */
BOOL vtconsole_write(VtConsole* vc, const char* buf, size_t len);

/* vtconsole_read: the bytes a VT terminal sends for the pending input.
 *
 * reads the events pending on the console input handle h without
 * blocking. key presses become UTF-8 or escape sequences, other events
 * (resize, focus, mouse, menu) are consumed and dropped.
 *
 *   HANDLE  h          console input handle
 *   char*   buf        receives the bytes
 *   size_t  len        size of buf, at least 16
 *   WCHAR*  surrogate  a high surrogate kept between calls, start at 0
 *
 *   returns int        bytes in buf, 0 when only other events were
 *                      pending, -1 with GetLastError() set
 */
int vtconsole_read(HANDLE h, char* buf, size_t len, WCHAR* surrogate);

/** @} */

#endif /* defined(_WIN32) */
#endif /* defined(VTCONSOLE_H) */
