#ifndef XTAGS_H
#define XTAGS_H

#include <stddef.h>

/**
 * \defgroup xtags xtags: HTML tag rules shared by the XML parsers
 * @{
 */

/* the void elements (br, img, input, ...) a parser closes by itself, null terminated */
extern const char* const xtag_html_self_closing[];

/* xtag_in_list: whether name (case-insensitive) is in the null terminated list tags.
 *
 * returns 1 or 0; a NULL list has no names. */
int xtag_in_list(const char* name, size_t namelen, const char* const* tags);

/* xtag_is_raw_text: whether name is script, style, textarea or title, elements whose content is
 * text up to the matching close tag, never markup. */
int xtag_is_raw_text(const char* name, size_t namelen);

/* xtag_is_implied_end: whether a start tag name closes the open element open_name first (HTML5
 * optional end tags: <li> closes <li>, a block element closes <p>, ...). Used by tolerant parsers. */
int xtag_is_implied_end(const char* name, size_t namelen, const char* open_name, size_t open_namelen);

/**
 * @}
 */

#endif
