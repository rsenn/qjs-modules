/* xtee.c: xtee_sink().
 * depends on: xevent.h.
 * rule: one member per event, each forwards to every sink in order. */

#define _GNU_SOURCE 1

#include "xml/xevent.h"

/**
 * \addtogroup xevent
 * @{
 */

#define XTEE_EACH(call) \
  ssize_t first = 0, r; \
  for(size_t i = 0; i < t->len; i++) { \
    const XmlEventSink* s = &t->sinks[i]; \
    r = (call); \
    if(r < 0) \
      return r; \
    if(i == 0) \
      first = r; \
  } \
  return first;

#define XTEE_TEXT(name, member) \
  static ssize_t name(void* o, const char* str, size_t len) { \
    XmlTee* t = o; \
    XTEE_EACH(s->member ? s->member(s->opaque, str, len) : 0) \
  }

XTEE_TEXT(xtee_error, error)
XTEE_TEXT(xtee_element_start, element_start)
XTEE_TEXT(xtee_element_end, element_end)
XTEE_TEXT(xtee_text, text)
XTEE_TEXT(xtee_comment, comment)
XTEE_TEXT(xtee_declaration, declaration)
XTEE_TEXT(xtee_instruction, instruction)
XTEE_TEXT(xtee_cdata, cdata)

static ssize_t
xtee_attribute(void* o, const char* name, size_t nlen, const char* value, size_t vlen) {
  XmlTee* t = o;

  XTEE_EACH(s->attribute ? s->attribute(s->opaque, name, nlen, value, vlen) : 0)
}

XmlEventSink
xtee_sink(XmlTee* tee) {
  return (XmlEventSink){
      tee,
      xtee_error,
      xtee_element_start,
      xtee_attribute,
      xtee_element_end,
      xtee_text,
      xtee_comment,
      xtee_declaration,
      xtee_instruction,
      xtee_cdata,
  };
}

/**
 * @}
 */
