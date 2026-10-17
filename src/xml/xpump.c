/* xpump.c: xread_pump(), xread_pump_finish(), xml_pump().
 * depends on: xevent.h (the sink), xread.h (push parser), xml.h (pull parser).
 * rule: one xevent_send() per parser event; the first sink failure stops all sending. */

#define _GNU_SOURCE 1

#include "xml/xevent.h"
#include "xml/xml.h"
#include "xml/xread.h"

typedef struct {
  const XmlEventSink* sink;
  const xr_state_t* state;
  ssize_t failure; /* first negative result of the sink, 0 if none */
} XmlPump;

static void
xpump_send(XmlPump* p, const XmlEvent* ev) {
  ssize_t r;

  if(p->failure)
    return;

  if((r = xevent_send(p->sink, ev)) < 0)
    p->failure = r;
}

static void
xpump_callback(xr_type_t type, const xr_str_t* name, const xr_str_t* value, void* user_data) {
  XmlPump* p = user_data;
  XmlEvent ev = {0};

  switch(type) {
    case xr_type_element_start: ev = (XmlEvent){.type = XE_ELEMENT_START, .name = name->cstr, .nlen = name->len}; break;
    case xr_type_element_end: ev = (XmlEvent){.type = XE_ELEMENT_END, .name = name ? name->cstr : NULL, .nlen = name ? name->len : 0}; break;
    case xr_type_attribute:
      ev = (XmlEvent){.type = XE_ATTRIBUTE, .name = name->cstr, .nlen = name->len, .value = value ? value->cstr : NULL, .vlen = value ? value->len : 0};
      break;
    case xr_type_text: ev = (XmlEvent){.type = XE_TEXT, .name = value->cstr, .nlen = value->len}; break;
    case xr_type_error:
      ev = (XmlEvent){.type = XE_ERROR, .name = value ? value->cstr : NULL, .nlen = value ? value->len : 0, .at = p->state->err_pos, .has_at = 1};
      break;
  }

  xpump_send(p, &ev);
}

ssize_t
xread_pump(xr_state_t* state, const XmlEventSink* sink, const char* chunk, size_t len) {
  XmlPump p = {sink, state, 0};

  xr_read(xpump_callback, chunk, len, &p, state);
  return p.failure;
}

ssize_t
xread_pump_finish(xr_state_t* state, const XmlEventSink* sink) {
  XmlPump p = {sink, state, 0};

  xr_finish(xpump_callback, &p, state);
  return p.failure;
}

ssize_t
xml_pump(XMLParser* xp, const XmlEventSink* sink) {
  XmlPump p = {sink, NULL, 0};

  for(;;) {
    xml_event_t type = xml_parser_run(xp);
    XmlEvent ev = {0};

    switch(type) {
      case XML_PARSE_OK: return p.failure ? p.failure : XML_PUMP_END;
      case XML_PARSE_AGAIN: return p.failure ? p.failure : XML_PUMP_AGAIN;
      case XML_PARSE_ERROR:
        ev = (XmlEvent){.type = XE_ERROR, .name = xp->event_name.data, .nlen = xp->event_name.len};
        break;
      case XML_ELEMENT_START:
      case XML_ELEMENT_END: ev = (XmlEvent){.type = (XmlEventType)type, .name = xp->event_name.data, .nlen = xp->event_name.len}; break;
      case XML_ATTRIBUTE:
        ev = (XmlEvent){.type = XE_ATTRIBUTE,
                        .name = xp->event_name.data,
                        .nlen = xp->event_name.len,
                        .value = xp->event_has_value ? xp->event_value.data : NULL,
                        .vlen = xp->event_has_value ? xp->event_value.len : 0};
        break;
      case XML_TEXT:
        if(!xp->event_has_value)
          continue;
        ev = (XmlEvent){.type = XE_TEXT, .name = xp->event_value.data, .nlen = xp->event_value.len};
        break;
      default: continue;
    }

    if(type == XML_ELEMENT_START || type == XML_ELEMENT_END || type == XML_PARSE_ERROR) {
      ev.at.line = xp->tag_line;
      ev.at.column = xp->tag_column;
      ev.has_at = 1;
    }

    xpump_send(&p, &ev);

    if(p.failure)
      return p.failure;

    if(type == XML_PARSE_ERROR)
      return XML_PUMP_ERROR;
  }
}
