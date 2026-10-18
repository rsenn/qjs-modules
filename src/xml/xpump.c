/* xpump.c: xr_feed(), xr_end(), xread_pump(), xread_pump_finish(), xml_pump().
 * depends on: xevent.h (sink, handler, source), xread.h (push parser), xml.h (pull parser).
 * rule: the first handler failure stops all delivery; the pumps are the handler form with a sink. */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include "xml/xml.h"
#include "xml/xread.h"

typedef struct {
  XmlEventHandler* handler;
  void* user;
  const xr_state_t* state;
  ssize_t failure; /* first negative result of the handler, 0 if none */
} XmlFeed;

static void
xfeed_send(XmlFeed* f, const XmlEvent* ev) {
  ssize_t r;

  if(f->failure)
    return;

  if((r = f->handler(f->user, ev)) < 0)
    f->failure = r;
}

static void
xfeed_callback(XmlEventType type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XmlFeed* f = user_data;
  XmlEvent ev = {0};

  switch(type) {
    case XE_ELEMENT_START:
      ev = (XmlEvent){
          .type = XE_ELEMENT_START,
          .name = name->cstr,
          .nlen = name->len,
      };
      break;
    case XE_ELEMENT_END:
      ev = (XmlEvent){
          .type = XE_ELEMENT_END,
          .name = name ? name->cstr : NULL,
          .nlen = name ? name->len : 0,
      };
      break;
    case XE_ATTRIBUTE:
      ev = (XmlEvent){
          .type = XE_ATTRIBUTE,
          .name = name->cstr,
          .nlen = name->len,
          .value = value ? value->cstr : NULL,
          .vlen = value ? value->len : 0,
      };
      break;
    case XE_TEXT:
      ev = (XmlEvent){
          .type = XE_TEXT,
          .str = value->cstr,
          .len = value->len,
      };
      break;
    case XE_ERROR:
      ev = (XmlEvent){
          .type = XE_ERROR,
          .str = value ? value->cstr : NULL,
          .len = value ? value->len : 0,
          .at = f->state->err_pos,
          .has_at = 1,
      };
      break;
    default: return; /* xread produces no markup events */
  }

  xfeed_send(f, &ev);
}

ssize_t
xr_feed(xr_state_t* state, const char* chunk, size_t len, XmlEventHandler* handler, void* user) {
  XmlFeed f = {handler, user, state, 0};

  xr_read(xfeed_callback, chunk, len, &f, state);
  return f.failure;
}

ssize_t
xr_end(xr_state_t* state, XmlEventHandler* handler, void* user) {
  XmlFeed f = {handler, user, state, 0};

  xr_finish(xfeed_callback, &f, state);
  return f.failure;
}

ssize_t
xread_pump(xr_state_t* state, const XmlEventSink* sink, const char* chunk, size_t len) {
  return xr_feed(state, chunk, len, xevent_handler, (void*)sink);
}

ssize_t
xread_pump_finish(xr_state_t* state, const XmlEventSink* sink) {
  return xr_end(state, xevent_handler, (void*)sink);
}

ssize_t
xml_pump(XMLParser* xp, const XmlEventSink* sink) {
  return xevent_pump(xml_source(xp), sink);
}
