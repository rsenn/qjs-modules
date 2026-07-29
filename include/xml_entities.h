#ifndef XML_ENTITIES_H
#define XML_ENTITIES_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \addtogroup xml
 * @{
 */

/*
 * Decodes XML character references in place: the five predefined entities
 * (&amp; &lt; &gt; &quot; &apos;) and numeric character references (&#NN; and
 * &#xHH;/&#XHH;), writing the UTF-8 encoding of the referenced code point.
 * Anything that isn't a recognized, well-terminated reference (a bare '&', an
 * unknown entity name, or a numeric reference with no ';' within a reasonable
 * distance) is left as-is, byte for byte - this is a best-effort decoder,
 * matching how permissive the rest of this module's XML parsing already is,
 * not a validating one (see BUGS: xml-no-entity-decoding).
 *
 * Shared by every reader in this module (src/xml.c's XMLParser, src/xread.c's
 * XMLPushParser, and quickjs-xml.c's legacy js_xml_parse()/xml.read()) so that
 * `write()`'s entity-*escaping* (XMLSerializer/xml.write()) round-trips through
 * any of them.
 *
 * Returns the new length, always <= len: decoding only ever shrinks a reference
 * (`&amp;` -> `&`, `&#65;` -> `A`, ...) or leaves bytes unchanged, never grows -
 * so this is always safe to do in place, into the very buffer it was passed.
 */
static inline size_t
xml_decode_entities(char* buf, size_t len) {
  size_t r = 0, w = 0;

  while(r < len) {
    size_t semi, namelen;
    const char* name;
    uint32_t cp = 0;
    int matched = 1;

    if(buf[r] != '&') {
      buf[w++] = buf[r++];
      continue;
    }

    /* look for a terminating ';' within a reasonable window (the longest
     * well-formed reference this function recognizes, "&#x10FFFF;", is 11
     * bytes; 32 gives generous slack without scanning arbitrarily far into
     * unrelated text on a bare '&'). */
    semi = r + 1;
    while(semi < len && semi - r <= 32 && buf[semi] != ';')
      semi++;

    if(semi >= len || buf[semi] != ';') {
      buf[w++] = buf[r++];
      continue;
    }

    namelen = semi - r - 1;
    name = buf + r + 1;

    if(namelen == 3 && !strncmp(name, "amp", 3))
      cp = '&';
    else if(namelen == 2 && !strncmp(name, "lt", 2))
      cp = '<';
    else if(namelen == 2 && !strncmp(name, "gt", 2))
      cp = '>';
    else if(namelen == 4 && !strncmp(name, "quot", 4))
      cp = '"';
    else if(namelen == 4 && !strncmp(name, "apos", 4))
      cp = '\'';
    else if(namelen >= 2 && name[0] == '#') {
      char* end = 0;

      if(namelen >= 3 && (name[1] == 'x' || name[1] == 'X'))
        cp = (uint32_t)strtoul(name + 2, &end, 16);
      else
        cp = (uint32_t)strtoul(name + 1, &end, 10);

      if(!end || end != name + namelen)
        matched = 0;
    } else {
      matched = 0;
    }

    if(!matched) {
      buf[w++] = buf[r++];
      continue;
    }

    if(cp < 0x80) {
      buf[w++] = (char)cp;
    } else if(cp < 0x800) {
      buf[w++] = (char)(0xC0 | (cp >> 6));
      buf[w++] = (char)(0x80 | (cp & 0x3F));
    } else if(cp < 0x10000) {
      buf[w++] = (char)(0xE0 | (cp >> 12));
      buf[w++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      buf[w++] = (char)(0x80 | (cp & 0x3F));
    } else {
      buf[w++] = (char)(0xF0 | (cp >> 18));
      buf[w++] = (char)(0x80 | ((cp >> 12) & 0x3F));
      buf[w++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      buf[w++] = (char)(0x80 | (cp & 0x3F));
    }

    r = semi + 1;
  }

  return w;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // #ifndef XML_ENTITIES_H
