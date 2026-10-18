#ifndef XOPTS_H
#define XOPTS_H

#include <quickjs.h>

/**
 * \addtogroup xml
 * @{
 */

/* XmlOptions: the options of xml.read() and of new XMLParser(input, options).
 *
 * ```js
 * { flat: true, tolerant: true, location: true, selfClosingTags: ['br', 'img'], filename: 'a.html' }
 * ```
 *
 *   flat             a flat list instead of a tree
 *   tolerant         a closing tag that matches nothing is skipped, not an error
 *   location         read() only: also return a WeakMap of positions
 *   selfClosingTags  tag names that never have an end tag, null-terminated; NULL means the HTML defaults of
 *                    read(), the parser itself has none
 *   filename         the name errors report */
typedef struct {
  int flat, tolerant, location;
  const char* const* self_closing_tags;
  char** owned; /* the array behind self_closing_tags when it came from JS */
  char* filename;
} XmlOptions;

/* xml_html_self_closing_tags: the HTML void elements, the default of xml.read(). null terminated. */
extern const char* const xml_html_self_closing_tags[];

/* xml_options_from_js: reads an options object into *o, a missing key keeps the value *o had.
 *
 * borrowed: the strings are owned, release them with xml_options_free().
 * returns 0, or -1 with an exception pending. */
int xml_options_from_js(JSContext*, JSValueConst options, XmlOptions* o);

void xml_options_free(JSContext*, XmlOptions* o);

/** @} */
#endif
