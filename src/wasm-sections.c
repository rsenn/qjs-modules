/* wasm-sections.c: wb_sections_parse(), wb_sections_free(), wb_put_u32().
 * depends on: wasm-sections.h.
 * rule: no engine calls; every read is bounds-checked against `end`. */

#include "wasm-sections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const uint8_t* p;
  const uint8_t* end;
  char* err;
  size_t errlen;
} Cursor;

/* a table or global, as the index spaces record them for the exports */
typedef struct {
  WBValType type;
  WBLimits limits;
  int is_mutable;
} Entry;

static int
fail(Cursor* c, const char* msg) {
  snprintf(c->err, c->errlen, "%s", msg);
  return -1;
}

/* reads an unsigned LEB128 of at most `maxbytes` bytes. returns 0, or -1
 * with the cursor's error set. */
static int
get_leb(Cursor* c, uint64_t* out, int maxbytes) {
  uint64_t v = 0;
  int shift = 0;

  for(int i = 0; i < maxbytes; i++) {
    if(c->p >= c->end)
      return fail(c, "unexpected end of binary");

    uint8_t b = *c->p++;

    v |= (uint64_t)(b & 0x7f) << shift;
    shift += 7;

    if(!(b & 0x80)) {
      *out = v;
      return 0;
    }
  }

  return fail(c, "malformed LEB128");
}

static int
get_u32(Cursor* c, uint32_t* out) {
  uint64_t v;

  if(get_leb(c, &v, 5))
    return -1;

  if(v > 0xffffffffu)
    return fail(c, "u32 out of range");

  *out = (uint32_t)v;
  return 0;
}

static int
get_byte(Cursor* c, uint8_t* out) {
  if(c->p >= c->end)
    return fail(c, "unexpected end of binary");

  *out = *c->p++;
  return 0;
}

static int
skip_bytes(Cursor* c, uint64_t n) {
  if(n > (uint64_t)(c->end - c->p))
    return fail(c, "unexpected end of binary");

  c->p += n;
  return 0;
}

static void*
alloc(WBSections* s, size_t n) {
  void** list;
  void* p = calloc(1, n ? n : 1);

  if(!p)
    return NULL;

  if(!(list = realloc(s->allocs, (s->nallocs + 1) * sizeof(void*)))) {
    free(p);
    return NULL;
  }

  s->allocs = list;
  list[s->nallocs++] = p;
  return p;
}

static int
get_valtype(Cursor* c, WBValType* out) {
  uint8_t b;

  if(get_byte(c, &b))
    return -1;

  switch(b) {
    case 0x7f: *out = WB_I32; return 0;
    case 0x7e: *out = WB_I64; return 0;
    case 0x7d: *out = WB_F32; return 0;
    case 0x7c: *out = WB_F64; return 0;
    case 0x70: *out = WB_FUNCREF; return 0;
    case 0x6f: *out = WB_EXTERNREF; return 0;
  }

  return fail(c, "unsupported value type");
}

/* limits: flag byte (0 = min, 1 = min and max), then the numbers. shared
 * (bit 1) and 64-bit (bit 2) limits are rejected. */
static int
get_limits(Cursor* c, WBLimits* out) {
  uint8_t flags;

  if(get_byte(c, &flags))
    return -1;

  if(flags > 1)
    return fail(c, "unsupported limits (shared or 64-bit)");

  out->has_max = flags & 1;
  out->max = 0;

  if(get_u32(c, &out->min))
    return -1;

  if(out->has_max && get_u32(c, &out->max))
    return -1;

  return 0;
}

/* a name: u32 length, then the bytes, copied and NUL-terminated. */
static int
get_name(Cursor* c, WBSections* s, const char** out) {
  uint32_t n;
  char* str;

  if(get_u32(c, &n))
    return -1;

  if(n > (size_t)(c->end - c->p))
    return fail(c, "unexpected end of binary");

  if(!(str = alloc(s, (size_t)n + 1)))
    return fail(c, "out of memory");

  memcpy(str, c->p, n);
  c->p += n;
  *out = str;
  return 0;
}

/* skips a constant expression up to and including its `end` (0x0b). */
static int
skip_const_expr(Cursor* c) {
  for(;;) {
    uint8_t op;
    uint64_t v;

    if(get_byte(c, &op))
      return -1;

    switch(op) {
      case 0x0b: return 0;
      case 0x41:
      case 0x42: /* i32.const, i64.const: signed LEB, skipped as unsigned */
        if(get_leb(c, &v, 10))
          return -1;
        break;
      case 0x43:
        if(skip_bytes(c, 4))
          return -1;
        break;
      case 0x44:
        if(skip_bytes(c, 8))
          return -1;
        break;
      case 0x23:
      case 0xd2:
        if(get_leb(c, &v, 5))
          return -1;
        break;
      case 0xd0:
        if(skip_bytes(c, 1))
          return -1;
        break;
      case 0x6a:
      case 0x6b:
      case 0x6c:
      case 0x7c:
      case 0x7d:
      case 0x7e: break; /* extended-const arithmetic, no immediates */
      default: return fail(c, "unsupported constant expression");
    }
  }
}

int
wb_sections_parse(WBSections* s, const uint8_t* bytes, size_t len, char* err, size_t errlen) {
  Cursor c = {bytes, bytes + len, err, errlen};
  WBFuncType* types = NULL;
  uint32_t ntypes = 0, nfuncs = 0, ntables = 0, nmems = 0, nglobals = 0;
  uint32_t* func_types = NULL;
  Entry* tables = NULL;
  WBLimits* mems = NULL;
  Entry* globals = NULL;
  size_t nimports_func = 0;

  memset(s, 0, sizeof(*s));

  if(len < 8 || memcmp(bytes, "\0asm\1\0\0\0", 8))
    return fail(&c, "bad magic number or version");

  c.p += 8;

  /* first pass sizes the index spaces; the sections come in a fixed order,
   * so one pass is enough: type < import < function < table < memory <
   * global < export. */
  while(c.p < c.end) {
    uint8_t id;
    uint32_t size;
    const uint8_t* start;
    const uint8_t* sec_end;
    size_t sec_off = (size_t)(c.p - bytes);

    if(get_byte(&c, &id) || get_u32(&c, &size))
      goto fail;

    if(size > (size_t)(c.end - c.p)) {
      fail(&c, "section extends past the end of the binary");
      goto fail;
    }

    start = c.p;
    sec_end = start + size;

    switch(id) {
      case 1: { /* type */
        uint32_t n;

        if(get_u32(&c, &n))
          goto fail;

        if(!(types = alloc(s, (size_t)n * sizeof(WBFuncType)))) {
          fail(&c, "out of memory");
          goto fail;
        }

        for(uint32_t i = 0; i < n; i++) {
          uint8_t form;
          uint32_t np, nr;
          WBValType* params;
          WBValType* results;

          if(get_byte(&c, &form))
            goto fail;

          if(form != 0x60) {
            fail(&c, "unsupported type form");
            goto fail;
          }

          if(get_u32(&c, &np))
            goto fail;

          params = alloc(s, (size_t)np * sizeof(WBValType));

          for(uint32_t j = 0; j < np; j++)
            if(!params || get_valtype(&c, &params[j])) {
              if(!params)
                fail(&c, "out of memory");
              goto fail;
            }

          if(get_u32(&c, &nr))
            goto fail;

          results = alloc(s, (size_t)nr * sizeof(WBValType));

          for(uint32_t j = 0; j < nr; j++)
            if(!results || get_valtype(&c, &results[j])) {
              if(!results)
                fail(&c, "out of memory");
              goto fail;
            }

          types[i] = (WBFuncType){params, np, results, nr};
        }

        ntypes = n;
        break;
      }

      case 2: { /* import */
        uint32_t n;

        s->import_off = sec_off;
        s->import_end = (size_t)(sec_end - bytes);

        if(get_u32(&c, &n))
          goto fail;

        s->imports = alloc(s, (size_t)n * sizeof(WBImportDesc));
        s->import_typeidx = alloc(s, (size_t)n * sizeof(uint32_t));
        func_types = alloc(s, ((size_t)n + 1) * sizeof(uint32_t));
        tables = alloc(s, ((size_t)n + 1) * sizeof(Entry));
        mems = alloc(s, ((size_t)n + 1) * sizeof(WBLimits));
        globals = alloc(s, ((size_t)n + 1) * sizeof(Entry));

        if(!s->imports || !s->import_typeidx || !func_types || !tables || !mems || !globals) {
          fail(&c, "out of memory");
          goto fail;
        }

        for(uint32_t i = 0; i < n; i++) {
          WBImportDesc* d = &s->imports[i];
          uint8_t kind;

          if(get_name(&c, s, &d->module) || get_name(&c, s, &d->name) || get_byte(&c, &kind))
            goto fail;

          switch(kind) {
            case 0: {
              uint32_t ti;

              if(get_u32(&c, &ti))
                goto fail;

              if(ti >= ntypes) {
                fail(&c, "import type index out of range");
                goto fail;
              }

              d->type.kind = WB_EXTERN_FUNC;
              d->type.u.func = types[ti];
              s->import_typeidx[i] = ti;
              func_types[nfuncs++] = ti;
              nimports_func++;
              break;
            }
            case 1: {
              WBValType et;
              WBLimits lim;

              if(get_valtype(&c, &et) || get_limits(&c, &lim))
                goto fail;

              d->type.kind = WB_EXTERN_TABLE;
              d->type.u.table.elem = et;
              d->type.u.table.limits = lim;
              tables[ntables++] = (Entry){et, lim, 0};
              break;
            }
            case 2: {
              WBLimits lim;

              if(get_limits(&c, &lim))
                goto fail;

              d->type.kind = WB_EXTERN_MEMORY;
              d->type.u.memory = lim;
              mems[nmems++] = lim;
              break;
            }
            case 3: {
              WBValType vt;
              uint8_t mut;

              if(get_valtype(&c, &vt) || get_byte(&c, &mut))
                goto fail;

              d->type.kind = WB_EXTERN_GLOBAL;
              d->type.u.global.type = vt;
              d->type.u.global.is_mutable = mut;
              globals[nglobals++] = (Entry){vt, {0, 0, 0}, mut};
              break;
            }
            default: fail(&c, "unsupported import kind (tag)"); goto fail;
          }
        }

        s->nimports = n;
        break;
      }

      case 3: { /* function: type index per defined function */
        uint32_t n;
        uint32_t* grown;

        if(get_u32(&c, &n))
          goto fail;

        if(!(grown = alloc(s, ((size_t)nfuncs + n + 1) * sizeof(uint32_t)))) {
          fail(&c, "out of memory");
          goto fail;
        }

        if(func_types)
          memcpy(grown, func_types, (size_t)nfuncs * sizeof(uint32_t));

        func_types = grown;

        for(uint32_t i = 0; i < n; i++) {
          uint32_t ti;

          if(get_u32(&c, &ti))
            goto fail;

          if(ti >= ntypes) {
            fail(&c, "function type index out of range");
            goto fail;
          }

          func_types[nfuncs++] = ti;
        }

        break;
      }

      case 4:
      case 5:
      case 6: { /* table, memory, global: defined ones follow the imported */
        uint32_t n;

        if(get_u32(&c, &n))
          goto fail;

        if(id == 4) {
          Entry* grown = alloc(s, ((size_t)ntables + n + 1) * sizeof(Entry));

          if(!grown) {
            fail(&c, "out of memory");
            goto fail;
          }

          if(tables)
            memcpy(grown, tables, (size_t)ntables * sizeof(Entry));

          tables = grown;

          for(uint32_t i = 0; i < n; i++) {
            WBValType et;
            WBLimits lim;

            if(get_valtype(&c, &et) || get_limits(&c, &lim))
              goto fail;

            tables[ntables++] = (Entry){et, lim, 0};
          }
        } else if(id == 5) {
          WBLimits* grown = alloc(s, ((size_t)nmems + n + 1) * sizeof(WBLimits));

          if(!grown) {
            fail(&c, "out of memory");
            goto fail;
          }

          if(mems)
            memcpy(grown, mems, (size_t)nmems * sizeof(WBLimits));

          mems = grown;

          for(uint32_t i = 0; i < n; i++)
            if(get_limits(&c, &mems[nmems++]))
              goto fail;
        } else {
          Entry* grown = alloc(s, ((size_t)nglobals + n + 1) * sizeof(Entry));

          if(!grown) {
            fail(&c, "out of memory");
            goto fail;
          }

          if(globals)
            memcpy(grown, globals, (size_t)nglobals * sizeof(Entry));

          globals = grown;

          for(uint32_t i = 0; i < n; i++) {
            WBValType vt;
            uint8_t mut;

            if(get_valtype(&c, &vt) || get_byte(&c, &mut) || skip_const_expr(&c))
              goto fail;

            globals[nglobals++] = (Entry){vt, {0, 0, 0}, mut};
          }
        }

        break;
      }

      case 7: { /* export */
        uint32_t n;

        if(get_u32(&c, &n))
          goto fail;

        if(!(s->exports = alloc(s, (size_t)n * sizeof(WBExportDesc)))) {
          fail(&c, "out of memory");
          goto fail;
        }

        for(uint32_t i = 0; i < n; i++) {
          WBExportDesc* d = &s->exports[i];
          uint8_t kind;
          uint32_t idx;

          if(get_name(&c, s, &d->name) || get_byte(&c, &kind) || get_u32(&c, &idx))
            goto fail;

          switch(kind) {
            case 0:
              if(idx >= nfuncs) {
                fail(&c, "export function index out of range");
                goto fail;
              }

              d->type.kind = WB_EXTERN_FUNC;
              d->type.u.func = types[func_types[idx]];
              break;
            case 1:
              if(idx >= ntables) {
                fail(&c, "export table index out of range");
                goto fail;
              }

              d->type.kind = WB_EXTERN_TABLE;
              d->type.u.table.elem = tables[idx].type;
              d->type.u.table.limits = tables[idx].limits;
              break;
            case 2:
              if(idx >= nmems) {
                fail(&c, "export memory index out of range");
                goto fail;
              }

              d->type.kind = WB_EXTERN_MEMORY;
              d->type.u.memory = mems[idx];
              break;
            case 3:
              if(idx >= nglobals) {
                fail(&c, "export global index out of range");
                goto fail;
              }

              d->type.kind = WB_EXTERN_GLOBAL;
              d->type.u.global.type = globals[idx].type;
              d->type.u.global.is_mutable = globals[idx].is_mutable;
              break;
            default: fail(&c, "unsupported export kind (tag)"); goto fail;
          }
        }

        s->nexports = n;
        break;
      }

      default: break;
    }

    c.p = sec_end;
  }

  (void)nimports_func;
  return 0;

fail:
  wb_sections_free(s);
  return -1;
}

void
wb_sections_free(WBSections* s) {
  for(size_t i = 0; i < s->nallocs; i++)
    free(s->allocs[i]);

  free(s->allocs);
  memset(s, 0, sizeof(*s));
}

size_t
wb_put_u32(uint8_t* dst, uint32_t v) {
  size_t n = 0;

  do {
    uint8_t b = v & 0x7f;

    v >>= 7;
    dst[n++] = v ? (b | 0x80) : b;
  } while(v);

  return n;
}
