/* vtparse.c: the VT/ANSI output parser behind ttyWrite on a console.
 * depends on: nothing but the handler it is given.
 * rule: every byte belongs to exactly one event or is dropped, unknown
 * sequences never reach the text callback. */

#include "vtparse.h"

/**
 * \addtogroup vtparse
 * @{
 */

#define VT_PARAM_MAX 65535

void
vt_init(VtParser* vp, const VtHandler* handler, void* opaque) {
  vp->state = VT_GROUND;
  vp->prefix = 0;
  vp->intermediate = 0;
  vp->ignore = 0;
  vp->nparams = 0;
  vp->handler = handler;
  vp->opaque = opaque;
}

int
vt_param(const VtParser* vp, int i, int def) {
  return i < vp->nparams && vp->params[i] >= 0 ? vp->params[i] : def;
}

static void
vt_control(VtParser* vp, int c) {
  if(vp->handler->control)
    vp->handler->control(vp->opaque, c);
}

static void
vt_csi_byte(VtParser* vp, unsigned char c) {
  if(c >= '0' && c <= '9') {
    int* p;

    if(vp->nparams == 0)
      vp->params[vp->nparams++] = -1;

    p = &vp->params[vp->nparams - 1];
    *p = *p < 0 ? 0 : *p;

    if(*p <= VT_PARAM_MAX)
      *p = *p * 10 + (c - '0');
  } else if(c == ';' || c == ':') {
    if(vp->nparams == 0)
      vp->params[vp->nparams++] = -1;

    if(vp->nparams < VT_MAX_PARAMS)
      vp->params[vp->nparams++] = -1;
    else
      vp->ignore = 1;
  } else if(c >= '<' && c <= '?') {
    if(vp->nparams || vp->prefix)
      vp->ignore = 1;
    else
      vp->prefix = c;
  } else if(c >= 0x20 && c <= 0x2f) {
    vp->intermediate = c;
  } else if(c >= 0x40 && c <= 0x7e) {
    if(!vp->ignore && vp->handler->csi)
      vp->handler->csi(vp->opaque, vp, c);

    vp->state = VT_GROUND;
  }
}

static void
vt_escape_byte(VtParser* vp, unsigned char c) {
  if(c == '[') {
    vp->state = VT_CSI;
    vp->prefix = 0;
    vp->intermediate = 0;
    vp->ignore = 0;
    vp->nparams = 0;
  } else if(vp->intermediate == 0 && (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_')) {
    vp->state = VT_STRING;
  } else if(c >= 0x20 && c <= 0x2f) {
    vp->intermediate = c;
  } else if(c >= 0x30 && c <= 0x7e) {
    if(vp->handler->esc)
      vp->handler->esc(vp->opaque, vp->intermediate, c);

    vp->state = VT_GROUND;
  } else {
    vp->state = VT_GROUND;
  }
}

void
vt_feed(VtParser* vp, const char* buf, size_t len) {
  size_t i, start = 0;

  for(i = 0; i < len; i++) {
    unsigned char c = buf[i];

    if(vp->state == VT_GROUND) {
      if(c >= 0x20 && c != 0x7f)
        continue;

      if(i > start && vp->handler->text)
        vp->handler->text(vp->opaque, buf + start, i - start);

      start = i + 1;

      if(c == 0x1b) {
        vp->state = VT_ESCAPE;
        vp->intermediate = 0;
      } else if(c != 0x7f) {
        vt_control(vp, c);
      }

      continue;
    }

    start = i + 1;

    switch(vp->state) {
      case VT_STRING:
        if(c == 0x07 || c == 0x18 || c == 0x1a)
          vp->state = VT_GROUND;
        else if(c == 0x1b)
          vp->state = VT_STRING_ESC;
        break;

      case VT_STRING_ESC: vp->state = c == 0x1b ? VT_STRING_ESC : c == '\\' ? VT_GROUND : VT_STRING; break;

      default:
        if(c == 0x1b) {
          vp->state = VT_ESCAPE;
          vp->intermediate = 0;
        } else if(c == 0x18 || c == 0x1a) {
          vp->state = VT_GROUND;
        } else if(c < 0x20) {
          vt_control(vp, c);
        } else if(vp->state == VT_ESCAPE) {
          vt_escape_byte(vp, c);
        } else {
          vt_csi_byte(vp, c);
        }
        break;
    }
  }

  if(vp->state == VT_GROUND && len > start && vp->handler->text)
    vp->handler->text(vp->opaque, buf + start, len - start);
}

/** @} */
