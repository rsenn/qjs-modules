#ifndef SRCPOS_H
#define SRCPOS_H

#include <stddef.h>
#include <stdint.h>

/**
 * \defgroup srcpos srcpos: source position tracking
 * @{
 */

/* SourcePos: where a parser is in its input, counted as bytes go through it.
 *
 * ```c
 * SourcePos p;
 * srcpos_init(&p, 1);                         // 1: columns in UTF-16 units
 * srcpos_advance(&p, "{\"a\":\n  [1, ", 12);   // p.line 2, p.column 8
 * ```
 *
 *   field        meaning                                         starts at
 *   byte_offset  bytes consumed                                  0
 *   char_offset  characters consumed (unit: see `utf16`)         0
 *   line         1 + line breaks consumed                        1
 *   column       1 + characters since the last line break        1
 *
 * Line breaks are "\n", "\r\n" (one break) and a lone "\r", as HTML and XML normalise
 * them. A character is a UTF-8 lead byte with its continuation bytes, so the count is
 * right when a chunk ends inside a character; with `utf16` set a 4-byte character
 * counts 2 (a surrogate pair), as JS strings and editors count. A tab is one column.
 * The U+FEFF byte order mark is a character like any other; strip it first.
 *
 * no engine, no allocation; header-only, so every piece can include it. */
typedef struct {
  size_t byte_offset, char_offset;
  uint32_t line, column;
  unsigned cr : 1;    /* the last byte was "\r": a following "\n" is the same break */
  unsigned utf16 : 1; /* count characters above U+FFFF as 2 */
} SourcePos;

static inline void
srcpos_init(SourcePos* pos, int utf16) {
  pos->byte_offset = pos->char_offset = 0;
  pos->line = pos->column = 1;
  pos->cr = 0;
  pos->utf16 = !!utf16;
}

/* srcpos_advance: counts `len` bytes at `buf` into `pos`.
 *
 * the result is the same whether the input arrives whole or in pieces of any size,
 * including a piece boundary inside a character or between "\r" and "\n". */
static inline void
srcpos_advance(SourcePos* pos, const void* buf, size_t len) {
  const uint8_t* p = buf;

  for(size_t i = 0; i < len; i++) {
    uint8_t c = p[i];

    pos->byte_offset++;

    if((c & 0xc0) == 0x80) /* continuation byte: part of the character already counted */
      continue;

    if(c == '\n' && pos->cr) { /* second half of "\r\n" */
      pos->cr = 0;
      pos->char_offset++;
      continue;
    }

    pos->cr = 0;
    pos->char_offset += pos->utf16 && c >= 0xf0 ? 2 : 1;

    if(c == '\n' || c == '\r') {
      pos->line++;
      pos->column = 1;
      pos->cr = c == '\r';
    } else {
      pos->column += pos->utf16 && c >= 0xf0 ? 2 : 1;
    }
  }
}

/**
 * @}
 */

#endif /* SRCPOS_H */
