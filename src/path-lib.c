/* path-lib.c: the lexical path functions, one file; (pointer, length) in,
 * caller's buffer out.
 * depends on: include/path.h (PATHSEP_C, path_islistsep), libc string.h.
 * rule: nothing is written unless the capacity allows the whole result. */
#include "path.h"
#include <string.h>

/* private helpers: every macro takes a view (p, n), never reads past n and
 * evaluates its arguments more than once, so pass plain lvalues.
 *
 *   SEP(c)                  c is a separator ('/', also '\\' on WINDOWS)
 *   DRIVE(p, n)             2 if p starts with "X:" (WINDOWS only), else 0
 *   SAMEROOT(a, ra, b, rb)  the roots a[0..ra), b[0..rb) are the same root
 *                           (off Windows: both or neither; "//" is "/")
 *   SEPLEN / COMLEN         run of separators / of non-separators at p
 *   RSEPLEN / RCOMLEN       the same, ending at p + n
 */
#define byte_copy(o, n, i) memmove((o), (i), (n))
#define byte_diff(a, n, b) memcmp((a), (b), (n))

#if defined(_WIN32) || defined(__MSYS__) || defined(__CYGWIN__)
#define WINDOWS 1
#define SEPCHARS "\\/"
#define SEP(c) ((c) == '/' || (c) == '\\')
#define DRIVE(p, n) (((n) >= 2 && (((p)[0] >= 'A' && (p)[0] <= 'Z') || ((p)[0] >= 'a' && (p)[0] <= 'z')) && (p)[1] == ':') ? 2 : 0)
#define SAMEROOT(a, ra, b, rb) ((ra) == (rb) && !byte_diff((a), (ra), (b)))
#else
#define WINDOWS 0
#define SEPCHARS "/"
#define SEP(c) ((c) == '/')
#define DRIVE(p, n) 0
#define SAMEROOT(a, ra, b, rb) (!(ra) == !(rb))
#endif

#define ISABS(p, n) (((n) >= 1 && SEP((p)[0])) || ((n) >= 3 && DRIVE(p, n) && SEP((p)[2])))

#define SEPLEN(p, n) path_scan((p), (n), 1, 0)
#define COMLEN(p, n) path_scan((p), (n), 0, 0)
#define RSEPLEN(p, n) path_scan((p), (n), 1, 1)
#define RCOMLEN(p, n) path_scan((p), (n), 0, 1)

/* length of the run of separators (sep != 0) or non-separators at the start
 * (rev == 0) or at the end of p[0..n) */
static size_t
path_scan(const char* p, size_t n, int sep, int rev) {
  size_t i = 0;

  if(rev)
    while(i < n && (SEP(p[n - 1 - i]) != 0) == sep)
      ++i;
  else
    while(i < n && (SEP(p[i]) != 0) == sep)
      ++i;

  return i;
}

/* "/x" is absolute; on Windows so is "\x" and "C:\x" ("C:" and "C:x" stay relative)
 *
 *   "some/dir"   -> 0
 *   "/some/dir"  -> 1
 *   "//some/dir" -> 2
 *   "c:/windows" -> 3
 *   "c:system32" -> 2
 */
size_t
path_rootlen(const char* x, size_t n) {
  if(DRIVE(x, n))
    return 2 + path_rootlen(x + 2, n - 2);

  return n > 0 ? SEPLEN(x, n) : 0;
}

/* last component of p[0..n), POSIX basename(1) rules, as a view; *len (if not NULL) gets its length
 *
 *   "/usr/lib"  -> "lib"
 *   "/usr/"     -> "usr"     trailing separators are not part of it
 *   "//"        -> "/"       only separators: the first one
 *   ""          -> ""
 * ----------------------------------------------------------------------- */
const char*
path_basename(const char* p, size_t n, size_t* len) {
  size_t end = n, start;

  if(n && SEP(*p) && SEPLEN(p, n) == n) {
    if(len)
      *len = 1;

    return p;
  }

  end -= RSEPLEN(p, end);
  start = end - RCOMLEN(p, end);

  if(len)
    *len = end - start;

  return p + start;
}

/* length of the directory part of s[0..n), as dirname(1) sees it
 *
 *   "a/b/c" -> 3
 *   "a//b/" -> 1
 *   "/a"    -> 1 (the root)
 *   "a"     -> 0
 *   "/"     -> 1
 *   ""      -> 0
 * ----------------------------------------------------------------------- */
size_t
path_dirlen(const char* s, size_t n) {
  if(n == 0)
    return 0;

  /* skip trailing path separators from the end */
  if((n -= RSEPLEN(s, n)) == 0)
    return 1;

  /* scan backwards past the current path component (non-separators) */
  if((n -= RCOMLEN(s, n)) == 0)
    return 0;

  /* skip backwards past any preceding consecutive separators */
  if((n -= RSEPLEN(s, n)) == 0)
    return 1;

  return n;
}

/* join a and b into out; returns the length the result needs, without the NUL.
 * Nothing is written unless cap > need; out may equal a.
 *
 *   "a","b" -> "a/b"   "a/","b" -> "a/b"   "","b" -> "b"   "a","" -> "a"   "a","/b" -> "/b"
 * ----------------------------------------------------------------------- */
size_t
path_cat(char* out, size_t cap, const char* a, size_t an, const char* b, size_t bn) {
  size_t need, sep = 0;

  if(bn == 0)
    need = an;
  else if(an == 0 || ISABS(b, bn))
    need = bn, an = 0;
  else {
    sep = !SEP(a[an - 1]);
    need = an + sep + bn;
  }

  if(cap > need) {
    if(an == 0)
      byte_copy(out, bn, b);
    else {
      if(out != a)
        byte_copy(out, an, a);

      if(bn) {
        if(sep)
          out[an] = PATHSEP_C;

        byte_copy(out + an + sep, bn, b);
      }
    }

    out[need] = '\0';
  }

  return need;
}

/* lexical canonical form of p[0..n) into out; returns its length.
 * out may equal p. Nothing is written unless cap >= n + 2, otherwise n + 1 (the bound) is returned.
 * Off Windows a leading run of separators is one (like realpath -m -s); "//host" stays on Windows.
 *
 *   "a//b/./c/../d" -> "a/b/d"   "/.." -> "/"   "../.." -> "../.."   "" -> "."   "a/.." -> "."
 *   "//a" -> "/a"
 * ----------------------------------------------------------------------- */
size_t
path_canon(char* out, size_t cap, const char* p, size_t n) {
  size_t root, ro, base, w, r, c;

  if(cap < n + 2)
    return n + 1;

  ro = root = path_rootlen(p, n);

#if !WINDOWS
  if(ro > 1)
    ro = 1;
#endif

  if(out != p)
    byte_copy(out, ro, p);

  w = base = ro;
  r = root + SEPLEN(p + root, n - root);

  while(r < n) {
    c = COMLEN(p + r, n - r);

    if(c == 1 && p[r] == '.') {
    } else if(c == 2 && p[r] == '.' && p[r + 1] == '.') {
      if(w > base) {
        while(w > base && !SEP(out[w - 1]))
          --w;

        if(w > base)
          --w;
      } else if(!ro) {
        if(w)
          out[w++] = PATHSEP_C;

        out[w++] = '.';
        out[w++] = '.';
        base = w;
      }
    } else {
      if(w > ro)
        out[w++] = PATHSEP_C;

      byte_copy(out + w, c, p + r);
      w += c;
    }

    r += c;
    r += SEPLEN(p + r, n - r);
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

  if(!SAMEROOT(a, ra, b, rb)) {
    if(blen)
      *blen = 0;
    return 0;
  }

  ia = ra + SEPLEN(a + ra, an - ra);
  ib = rb + SEPLEN(b + rb, bn - rb);
  ea = ra;
  eb = rb;

  for(;;) {
    ca = COMLEN(a + ia, an - ia);
    cb = COMLEN(b + ib, bn - ib);

    if(ca == 0 || ca != cb || byte_diff(a + ia, ca, b + ib))
      break;

    ea = ia + ca;
    eb = ib + cb;
    ia = ea + SEPLEN(a + ea, an - ea);
    ib = eb + SEPLEN(b + eb, bn - eb);
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

  if(!SAMEROOT(p, path_rootlen(p, pn), base, path_rootlen(base, bn)))
    return (size_t)-1;

  ca = path_common(p, pn, base, bn, &cb);
  rest = p + ca + SEPLEN(p + ca, pn - ca);
  pn -= rest - p;

  while(pn && SEP(rest[pn - 1]))
    --pn;

  for(i = cb + SEPLEN(base + cb, bn - cb); i < bn; i += COMLEN(base + i, bn - i), i += SEPLEN(base + i, bn - i))
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
