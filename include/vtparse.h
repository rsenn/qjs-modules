#ifndef VTPARSE_H
#define VTPARSE_H

#include <stddef.h>

/**
 * \defgroup vtparse vtparse: VT/ANSI output sequence parser
 *
 * the DEC VT500 state machine, reduced to what a backend acts on:
 *   - events: text runs, C0 controls, ESC and CSI sequences
 *   - OSC, DCS and other strings are consumed and dropped
 *   - bytes from 0x80 up are text, so UTF-8 passes through
 * @{
 */

#define VT_MAX_PARAMS 16

typedef enum {
  VT_GROUND = 0,
  VT_ESCAPE,
  VT_CSI,
  VT_STRING,
  VT_STRING_ESC,
} VtState;

struct vt_parser;

/* what the parser calls, a NULL member drops that kind of event */
typedef struct vt_handler {
  void (*text)(void* opaque, const char* s, size_t len);
  void (*control)(void* opaque, int c);
  void (*esc)(void* opaque, int intermediate, int final);
  void (*csi)(void* opaque, const struct vt_parser* vp, int final);
} VtHandler;

typedef struct vt_parser {
  VtState state;
  char prefix;       /* CSI private marker, one of < = > ?, or 0 */
  char intermediate; /* last byte 0x20-0x2f before the final, or 0 */
  unsigned ignore : 1;
  int nparams;
  int params[VT_MAX_PARAMS]; /* -1 where the parameter was omitted */
  const VtHandler* handler;
  void* opaque;
} VtParser;

/* vt_init: prepares vp to call handler with opaque. never fails. */
void vt_init(VtParser* vp, const VtHandler* handler, void* opaque);

/* vt_feed: parses len bytes of buf, calling the handler per event.
 *
 * a sequence split across two calls is completed by the second one,
 * a text run is reported per call and never crosses a control byte.
 */
void vt_feed(VtParser* vp, const char* buf, size_t len);

/* vt_param: CSI parameter i, or def when it is omitted or missing.
 *
 *   CSI 5;;7 H   gives  vt_param(vp, 1, 1) == 1, vt_param(vp, 2, 1) == 7
 */
int vt_param(const VtParser* vp, int i, int def);

/** @} */

#endif /* defined(VTPARSE_H) */
