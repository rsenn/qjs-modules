#ifndef WASM_SECTIONS_H
#define WASM_SECTIONS_H

#include "wasm-backend.h"

/* wasm-sections.h: reads a .wasm binary's type, import and export sections.
 * depends on: wasm-backend.h (the descriptor structs it fills in).
 * rule: runtime-independent; both backends list imports and exports
 * through it instead of through their engine's internals. */

typedef struct WBCustomSection {
  const char* name;
  size_t off; /* payload offset into the binary */
  size_t len;
} WBCustomSection;

typedef struct WBSections {
  WBImportDesc* imports; /* in binary order */
  size_t nimports;
  uint32_t* import_typeidx; /* per import: type index when a function, else 0 */
  WBExportDesc* exports;    /* in binary order */
  size_t nexports;
  WBCustomSection* customs; /* in binary order */
  size_t ncustoms;
  size_t import_off; /* offset of the import section's id byte, 0 if none */
  size_t import_end; /* offset just past the import section */
  void** allocs;     /* every block the parse allocated, freed together */
  size_t nallocs;
} WBSections;

/* parses `bytes`' type, import, function, table, memory, global and export
 * sections; the rest is skipped, not validated.
 *
 *   WBSections*     s       zeroed on entry, filled in on success
 *   const uint8_t*  bytes   the whole binary
 *   size_t          len     its length
 *   char*           err     receives a message on failure
 *   size_t          errlen  size of `err`
 *
 *   returns int     0, or -1 with `err` set (truncated binary, bad magic,
 *                   or a feature the backends do not model: SIMD, tags,
 *                   shared or 64-bit memories)
 */
int wb_sections_parse(WBSections* s, const uint8_t* bytes, size_t len, char* err, size_t errlen);

/* finds the `idx`-th custom section called `name` in `bytes`, the binary
 * that was parsed. never throws: returns 0 when found, 1 when not. */
int wb_sections_custom(const WBSections* s, const uint8_t* bytes, const char* name, size_t idx, const uint8_t** data, size_t* len);

/* frees everything wb_sections_parse() allocated. never throws. */
void wb_sections_free(WBSections* s);

/* writes `v` as an unsigned LEB128 at `dst`. never throws: returns the
 * number of bytes written (1 to 5). */
size_t wb_put_u32(uint8_t* dst, uint32_t v);

#endif /* WASM_SECTIONS_H */
