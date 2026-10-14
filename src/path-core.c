/* path-core.c: the lexical path functions shared with c-utils and shish (lib/path/).
 * Pure functions over (pointer, length); the caller owns every buffer.
 * depends on: nothing but include/path.h macros (path_issep, PATHSEP_C).
 * rule: nothing is written unless the capacity allows the whole result. */
#include "path.h"
#include <string.h>

#define byte_copy(o, n, i) memmove((o), (i), (n))
#define byte_diff(a, n, b) memcmp((a), (b), (n))

#if defined(_WIN32) || defined(__MSYS__) || defined(__CYGWIN__)
#define path_isdrive(p) (((((p)[0] >= 'A' && (p)[0] <= 'Z') || ((p)[0] >= 'a' && (p)[0] <= 'z'))) && (p)[1] == ':')
#else
#define path_isdrive(p) 0
#endif

static size_t
path_seplen(const char* p, size_t n) {
  size_t i = 0;

  while(i < n && path_issep(p[i]))
    ++i;

  return i;
}

static size_t
path_comlen(const char* p, size_t n) {
  size_t i = 0;

  while(i < n && !path_issep(p[i]))
    ++i;

  return i;
}

/* "/x" is absolute; on Windows so is "\x" and "C:\x" ("C:" and "C:x" stay relative) */
size_t
path_rootlen(const char* x, size_t n) {
  if(n > 0 && path_issep(x[0]))
    return 1;

  if(n >= 2 && path_isdrive(x))
    return n >= 3 && path_issep(x[2]) ? 3 : 2;

  return 0;
}

/* path_basename returns the last component of p[0..n) as a view; *len (if not NULL) gets its length.
 *
 *   "/usr/lib" -> "lib"    "usr/" -> "usr"    "/" -> "/"    "" -> ""
 */
const char*
path_basename(const char* p, size_t n, size_t* len) {
  size_t root = path_rootlen(p, n), end = n, start;

  while(end > root && path_issep(p[end - 1]))
    --end;

  start = end;

  while(start > root && !path_issep(p[start - 1]))
    --start;

  if(start == end && root) {
    start = 0;
    end = root;
  }

  if(len)
    *len = end - start;

  return p + start;
}

/* path_dirlen returns the length of the directory part of p[0..n), root-aware; 0 if there is none.
 *
 *   "/usr/lib/" -> 4 ("/usr")    "/usr" -> 1 ("/")    "a//b" -> 1 ("a")    "usr" -> 0
 */
size_t
path_dirlen(const char* p, size_t n) {
  size_t root = path_rootlen(p, n), e = n;

  while(e > root && path_issep(p[e - 1]))
    --e;

  while(e > root && !path_issep(p[e - 1]))
    --e;

  while(e > root && path_issep(p[e - 1]))
    --e;

  return e;
}

/* path_cat joins a and b into out and returns the length the result needs, without the NUL.
 * Nothing is written unless cap > need; out may equal a.
 *
 *   "a","b" -> "a/b"   "a/","b" -> "a/b"   "","b" -> "b"   "a","" -> "a"   "a","/b" -> "/b"
 */
size_t
path_cat(char* out, size_t cap, const char* a, size_t an, const char* b, size_t bn) {
  size_t need, sep = 0;

  if(bn == 0)
    need = an;
  else if(an == 0 || (path_rootlen(b, bn) & 1))
    need = bn, an = 0;
  else {
    sep = !path_issep(a[an - 1]);
    need = an + sep + bn;
  }

  if(cap > need) {
    if(an == 0)
      byte_copy(out, bn, b);
    else if(bn) {
      if(sep)
        out[an] = PATHSEP_C;
      byte_copy(out + an + sep, bn, b);
    }

    if(out != a && an)
      byte_copy(out, an, a);

    out[need] = '\0';
  }

  return need;
}

/* path_canon writes the lexical canonical form of p[0..n) into out and returns its length.
 * out may equal p. Nothing is written unless cap >= n + 2; otherwise n + 1 (the upper bound) is returned.
 *
 *   "a//b/./c/../d" -> "a/b/d"   "/.." -> "/"   "../.." -> "../.."   "" -> "."   "a/.." -> "."
 */
size_t
path_canon(char* out, size_t cap, const char* p, size_t n) {
  size_t root, base, w, r, c;

  if(cap < n + 2)
    return n + 1;

  root = path_rootlen(p, n);

  if(out != p)
    byte_copy(out, root, p);

  w = base = root;
  r = root + path_seplen(p + root, n - root);

  while(r < n) {
    c = path_comlen(p + r, n - r);

    if(c == 1 && p[r] == '.') {
    } else if(c == 2 && p[r] == '.' && p[r + 1] == '.') {
      if(w > base) {
        while(w > base && !path_issep(out[w - 1]))
          --w;

        if(w > base)
          --w;
      } else if(!root) {
        if(w)
          out[w++] = PATHSEP_C;

        out[w++] = '.';
        out[w++] = '.';
        base = w;
      }
    } else {
      if(w > root)
        out[w++] = PATHSEP_C;

      byte_copy(out + w, c, p + r);
      w += c;
    }

    r += c;
    r += path_seplen(p + r, n - r);
  }

  if(w == 0)
    out[w++] = '.';

  out[w] = '\0';
  return w;
}

/* path_abs writes the canonical absolute form of p, relative to cwd, into out and returns its length.
 * Nothing is written unless cap >= cwdn + n + 3, otherwise cwdn + n + 2 (the bound) is returned.
 *
 *   "x/../y","/a/b" -> "/a/b/y"   "/z","/a/b" -> "/z"   ".","/a/b" -> "/a/b"
 * ----------------------------------------------------------------------- */
size_t
path_abs(char* out, size_t cap, const char* p, size_t n, const char* cwd, size_t cwdn) {
  size_t m = cwdn + n + 2;

  if(cap < m + 1)
    return m;

  m = path_cat(out, cap, cwd, cwdn, p, n);
  return path_canon(out, cap, out, m);
}

/* path_common returns the length of the common whole-component prefix of a and b, in a, and sets *blen
 * to its length in b; trailing separators are excluded, repeated separators compare equal.
 *
 *   "/usr/local/bin","//usr//local//share" -> 10, *blen 12    "a/b","c/b" -> 0, 0    "/a","a" -> 0, 0
 */
size_t
path_common(const char* a, size_t an, const char* b, size_t bn, size_t* blen) {
  size_t ra = path_rootlen(a, an), rb = path_rootlen(b, bn), ia, ib, ea, eb, ca, cb;

  if(ra != rb || byte_diff(a, ra, b)) {
    if(blen)
      *blen = 0;
    return 0;
  }

  ia = ra + path_seplen(a + ra, an - ra);
  ib = rb + path_seplen(b + rb, bn - rb);
  ea = ra;
  eb = rb;

  for(;;) {
    ca = path_comlen(a + ia, an - ia);
    cb = path_comlen(b + ib, bn - ib);

    if(ca == 0 || ca != cb || byte_diff(a + ia, ca, b + ib))
      break;

    ea = ia + ca;
    eb = ib + cb;
    ia = ea + path_seplen(a + ea, an - ea);
    ib = eb + path_seplen(b + eb, bn - eb);
  }

  if(blen)
    *blen = eb;

  return ea;
}

/* path_rel writes the lexical relative path from base to p into out and returns its length.
 * Both must be canonical (see path_canon). Nothing is written unless cap > length.
 * Returns (size_t)-1 if one is absolute and the other relative, or their roots differ.
 *
 *   "/a/b/c","/a/d" -> "../b/c"   "/a","/a" -> "."   "a","a/b/c" -> "../.."   "/a","b" -> -1
 */
size_t
path_rel(char* out, size_t cap, const char* p, size_t pn, const char* base, size_t bn) {
  size_t ca, cb, up = 0, i, len;
  const char* rest;

  if(path_rootlen(p, pn) != path_rootlen(base, bn) || byte_diff(p, path_rootlen(p, pn), base))
    return (size_t)-1;

  ca = path_common(p, pn, base, bn, &cb);
  rest = p + ca + path_seplen(p + ca, pn - ca);
  pn -= rest - p;

  while(pn && path_issep(rest[pn - 1]))
    --pn;

  for(i = cb + path_seplen(base + cb, bn - cb); i < bn; i += path_comlen(base + i, bn - i), i += path_seplen(base + i, bn - i))
    ++up;

  len = up * 2 + (up ? up - 1 : 0) + (up && pn ? 1 : 0) + pn;

  if(len == 0)
    len = 1;

  if(cap > len) {
    char* o = out;

    for(i = 0; i < up; ++i) {
      if(i)
        *o++ = PATHSEP_C;

      *o++ = '.';
      *o++ = '.';
    }

    if(up && pn)
      *o++ = PATHSEP_C;

    byte_copy(o, pn, rest);
    o += pn;

    if(o == out)
      *o++ = '.';

    *o = '\0';
  }

  return len;
}

/* path_extoff returns the offset of the extension dot in the basename of p[0..n), or n if it has none.
 * A leading dot is not an extension; the last dot wins.
 *
 *   "a/b.tar.gz" -> offset of ".gz"   ".bashrc" -> n   "a.d/b" -> n   "a." -> offset of "."
 */
size_t
path_extoff(const char* p, size_t n) {
  size_t len, i;
  const char* b = path_basename(p, n, &len);

  if(len == 2 && b[0] == '.' && b[1] == '.')
    return n;

  for(i = len; i > 1; --i)
    if(b[i - 1] == '.')
      return (size_t)(b - p) + i - 1;

  return n;
}


/* PATH-style lists: elements are separated by PATHLISTSEP_CHARS (";" on Windows, ":" or ";" elsewhere);
 * an empty element is the current directory.  "a::b:" -> "a", "", "b", "" */
size_t
path_listlen(const char* l, size_t n) {
  size_t i = 0;

  while(i < n && !path_islistsep(l[i]))
    ++i;

  return i;
}

/* start of the element after the one at `pos`, (size_t)-1 after the last */
size_t
path_listnext(const char* l, size_t n, size_t pos) {
  size_t e = pos + path_listlen(l + pos, n - pos);

  return e < n ? e + 1 : (size_t)-1;
}

/* writes the first "element/name" of list l that ok() accepts to out and returns its length, 0 if none */
size_t
path_search(char* out, size_t cap, const char* l, size_t ln, const char* name, size_t nl, int (*ok)(const char*, void*), void* arg) {
  const char* e;
  size_t el, n;

  PATH_LIST_FOREACH(l, ln, e, el) {
    n = path_cat(out, cap, e, el, name, nl);

    if(n < cap && ok(out, arg))
      return n;
  }

  return 0;
}
