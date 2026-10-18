/* xnext.c: xml_next(), xml_source(); the pull parser as an XmlEvent source.
 * depends on: xml.h (xml_parse), xevent.h (the event).
 * rule: xml_parse() does the parsing, this types its markup and fills the event. */

#define _GNU_SOURCE 1

#include "xml/xml.h"
#include <string.h>

/**
 * \addtogroup xml
 * @{
 */

XmlPull
xml_next(XMLParser* p, XmlEvent* ev) {
  for(;;) {
    int type = p->mk_replay ? p->mk_type : xml_parse(p);
    int at = 1;

    *ev = (XmlEvent){0};

    /* a processing instruction has no end event: the first event that is not an attribute, or the
       end of the input, ends it; the event is parsed already, so it is kept for the next call */
    if(p->mk_pi && type != XE_ATTRIBUTE && type != XML_PARSE_AGAIN && type != XE_ELEMENT_END) {
      p->mk_pi = 0;
      p->mk_replay = type != XML_PARSE_OK;
      p->mk_type = type;
      ev->type = XE_INSTRUCTION;
      ev->str = (const char*)p->mk.buf;
      ev->len = p->mk.size;

      if(ev->len && ev->str[ev->len - 1] == '?')
        ev->len--;

      ev->at.line = p->tag_line;
      ev->at.column = p->tag_column;
      ev->has_at = 1;
      return XML_PULL_EVENT;
    }

    p->mk_replay = 0;

    switch(type) {
      case XML_PARSE_OK: return XML_PULL_END;
      case XML_PARSE_AGAIN: return XML_PULL_AGAIN;
      case XE_ERROR:
        ev->type = XE_ERROR;
        ev->str = p->err_code;
        ev->len = p->err_code ? strlen(p->err_code) : 0;
        ev->code = p->err_code;
        ev->span = p->err_len;
        break;
      case XE_ELEMENT_START:
        if(p->event_name.len && p->event_name.data[0] == '!') {
          xevent_markup(ev, p->event_name.data, p->event_name.len);
          p->mk_skip_end = 1;
        } else if(p->event_name.len && p->event_name.data[0] == '?') {
          p->mk.size = 0;
          dbuf_put(&p->mk, (const uint8_t*)p->event_name.data + 1, p->event_name.len - 1);
          p->mk_pi = 1;
          continue;
        } else {
          ev->type = XE_ELEMENT_START;
          ev->name = p->event_name.data;
          ev->nlen = p->event_name.len;
        }
        break;
      case XE_ATTRIBUTE:
        if(p->mk_pi) {
          dbuf_putc(&p->mk, ' ');
          dbuf_put(&p->mk, (const uint8_t*)p->event_name.data, p->event_name.len);

          if(p->event_has_value) {
            dbuf_put(&p->mk, (const uint8_t*)"=\"", 2);
            dbuf_put(&p->mk, (const uint8_t*)p->event_value.data, p->event_value.len);
            dbuf_putc(&p->mk, '"');
          }

          continue;
        }

        ev->type = XE_ATTRIBUTE;
        ev->name = p->event_name.data;
        ev->nlen = p->event_name.len;
        ev->value = p->event_has_value ? (p->event_value.data ? p->event_value.data : "") : NULL;
        ev->vlen = p->event_has_value ? p->event_value.len : 0;
        break;
      case XE_ELEMENT_END:
        if(p->mk_skip_end) {
          p->mk_skip_end = 0;
          continue;
        }

        if(p->mk_pi) {
          p->mk_pi = 0;
          ev->type = XE_INSTRUCTION;
          ev->str = (const char*)p->mk.buf;
          ev->len = p->mk.size;

          if(ev->len && ev->str[ev->len - 1] == '?')
            ev->len--;

          break;
        }

        ev->type = XE_ELEMENT_END;
        ev->name = p->event_name.data;
        ev->nlen = p->event_name.len;
        ev->self_closing = p->self_closing;
        break;
      case XE_TEXT:
        if(!p->event_has_value)
          continue;

        ev->type = XE_TEXT;
        ev->str = p->event_value.data;
        ev->len = p->event_value.len;
        at = 0;
        break;
      default: continue;
    }

    if(at) {
      ev->at.line = p->tag_line;
      ev->at.column = p->tag_column;
      ev->has_at = 1;
    }

    return XML_PULL_EVENT;
  }
}

static XmlPull
xnext_step(void* opaque, XmlEvent* ev) {
  return xml_next(opaque, ev);
}

XmlEventSource
xml_source(XMLParser* p) {
  return (XmlEventSource){p, xnext_step};
}

/**
 * @}
 */
