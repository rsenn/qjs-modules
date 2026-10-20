/* path-lib.c: the lexical path functions, one file; (pointer, length) in,
 * caller's buffer out.
 * depends on: include/path.h (PATHSEP_C, path_islistsep), libc string.h.
 * rule: nothing is written unless the capacity allows the whole result. */
#include "path.h"
#include <string.h>
#include <ctype.h>

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

/* 1 if p[0..n) is absolute: its root contains a separator; on Windows "C:/x" too, not "C:" or "C:x"
 *
 *   "/" -> 1   "//a" -> 1   "a/b" -> 0   "" -> 0   "./a" -> 0
 */
int
path_isabs(const char* p, size_t n) {
  return ISABS(p, n);
}

/* 1 if p[0..n) is not absolute (see path_isabs); the empty path is relative
 *
 *   "a/b" -> 1   "" -> 1   "/a" -> 0
 */
int
path_isrel(const char* p, size_t n) {
  return !ISABS(p, n);
}

/* 1 if p[0..n) has no separator, i.e. a bare command name to search a list for, 0 if it is a path
 *
 *   "ls" -> 1   "./ls" -> 0   "a/b" -> 0   "" -> 1
 */
int
path_isname(const char* p, size_t n) {
  return COMLEN(p, n) == n;
}

/* end of the first component of p[0..n) that starts at or after pos, or 0 if none is left. The root
 * (see path_rootlen) is skipped, so a loop over the prefixes starts with the first name:
 *
 * ```c
 * for(k = 0; (k = path_next(p, n, k)); ) mkdir(p[0..k)) ...
 * ```
 *
 *   "/a//b/" : 0 -> 2 ("/a"), 2 -> 5 ("/a//b"), 5 -> 0
 *   "a"      : 0 -> 1, 1 -> 0
 *   "/", ""  : 0 -> 0
 */
size_t
path_next(const char* p, size_t n, size_t pos) {
  size_t root = path_rootlen(p, n);

  if(pos < root)
    pos = root;

  if(pos > n)
    return 0;

  pos += SEPLEN(p + pos, n - pos);

  if(pos >= n)
    return 0;

  return pos + COMLEN(p + pos, n - pos);
}

/* length of the parent of p[0..n), without the separators between them: always < n, or 0 when p has
 * no parent; the root is a parent. A loop over the ancestors, nearest first, ends by itself:
 *
 * ```c
 * for(k = path_parentlen(p, n); k; k = path_parentlen(p, k)) rmdir(p[0..k)) ...
 * ```
 *
 *   "a/b/c" -> 3    "a//b/" -> 1    "/a/b" -> 2    "/a" -> 1    "//a" -> 2
 *   "a", "a/", "/", "" -> 0
 */
size_t
path_parentlen(const char* p, size_t n) {
  size_t root = path_rootlen(p, n), m = n - RSEPLEN(p, n), k;

  if(m <= root)
    return 0;

  m -= RCOMLEN(p, m);

  if(m <= root)
    return root;

  k = RSEPLEN(p, m);
  return m - k < root ? root : m - k;
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

/* fnmatch(3) on (pointer, length) views, so the string need not be NUL-terminated
 * (dietlibc fnmatch by Felix Leitner, extended with POSIX bracket classes). */
/* Based on dietlibc fnmatch (C) by Felix Leitner
 *
 * rewritten to support strings that are not nul-terminated
 */
/* does character c belong to POSIX bracket class "name" (the "lower"
 * in "[:lower:]"), name/namelen not including the surrounding "[:"/
 * ":]"? Only the classes POSIX 2.13.1 actually lists are recognized;
 * an unknown class name matches nothing, same as glibc.
 * ----------------------------------------------------------------------- */
static int
path_fnmatch_class(const char* name, size_t namelen, int c) {
#define CLASS(s, fn) \
  if(namelen == sizeof(s) - 1 && !memcmp(name, s, namelen)) \
  return !!fn((unsigned char)c)
  CLASS("upper", isupper);
  CLASS("lower", islower);
  CLASS("alpha", isalpha);
  CLASS("digit", isdigit);
  CLASS("alnum", isalnum);
  CLASS("punct", ispunct);
  CLASS("graph", isgraph);
  CLASS("print", isprint);
  CLASS("cntrl", iscntrl);
  CLASS("blank", isblank);
  CLASS("space", isspace);
  CLASS("xdigit", isxdigit);
#undef CLASS
  return 0;
}

/* find the end of a "[:class:]"/"[.symbol.]"/"[=equiv=]" bracket
 * sub-expression starting at "pattern" (pattern[0] == '[', pattern[1]
 * is the delimiter char "delim"): returns the number of pattern bytes
 * it occupies (including both delimiter pairs), or 0 if "plen" bytes
 * aren't enough to find a closing "delim]" -- the caller falls back
 * to treating "[" as an ordinary bracket-expression character in that
 * case, same as an unterminated "[:"/"[."/"[=" always has.
 * ----------------------------------------------------------------------- */
static size_t
path_fnmatch_subexpr_len(const char* pattern, size_t plen, char delim) {
  size_t i;

  for(i = 2; i + 1 < plen; i++) {
    if(pattern[i] == delim && pattern[i + 1] == ']')
      return i + 2;
  }

  return 0;
}

/* if the "plen" bytes at "pattern" form a valid range suffix ("-"
 * followed by a second endpoint that isn't the bracket's closing
 * "]"), resolves that second endpoint -- which may itself be a
 * "[.symbol.]"/"[=equiv=]" single-character collating construct
 * (e.g. the "-[.2.]" in "[[.0.]-[.2.]]"), not just a bare character --
 * and returns 1, filling in *end (the endpoint's character value) and
 * *skip (how many pattern bytes the whole "-<endpoint>" occupies).
 * Returns 0 (not a range -- a trailing "-]", or a multi-character/
 * class endpoint this implementation can't resolve to a single
 * character) without touching *end / *skip otherwise.
 * ----------------------------------------------------------------------- */
static int
path_fnmatch_range_end(const char* pattern, size_t plen, char* end, size_t* skip) {
  if(plen < 2 || pattern[0] != '-' || pattern[1] == ']')
    return 0;

  if(plen >= 3 && pattern[1] == '[' && (pattern[2] == '.' || pattern[2] == '=')) {
    size_t sublen = path_fnmatch_subexpr_len(pattern + 1, plen - 1, pattern[2]);

    if(sublen == 0 || sublen - 4 != 1)
      return 0;

    *end = pattern[3];
    *skip = 1 + sublen;
    return 1;
  }

  *end = pattern[1];
  *skip = 2;
  return 1;
}

/* does the bracket expression whose "[" is at pattern[0] have a closing "]"?
 * no: the "[" is a literal, e.g. case [[ in [[) or a[ matching "a[".
 * ----------------------------------------------------------------------- */
static int
path_fnmatch_has_close(const char* pattern, size_t plen, int flags) {
  size_t j = 1, n;

  if(j < plen && pattern[j] == '!')
    j++;

  /* a "]" first in the class is a member, not the end */
  for(j++; j < plen; j++) {
    if(pattern[j] == '\\' && !(flags & PATH_FNM_NOESCAPE))
      j++;
    else if(pattern[j] == '[' && j + 1 < plen &&
            (pattern[j + 1] == ':' || pattern[j + 1] == '.' || pattern[j + 1] == '=') &&
            (n = path_fnmatch_subexpr_len(pattern + j, plen - j, pattern[j + 1])))
      j += n - 1;
    else if(pattern[j] == ']')
      return 1;
  }

  return 0;
}

int
path_fnmatch(const char* pattern, size_t plen, const char* string, size_t slen, int flags) {
  /* backtrack bookmark for the most recently seen '*': whenever
   * matching fails below (any "goto fail"), if this is set we retry
   * by letting that '*' consume one more character of string and
   * resuming the match from right after it, instead of failing
   * outright -- the standard iterative technique for wildcard
   * matching (see e.g. the well-known "wildcard matching" two-pointer
   * algorithm). Only the single most recent '*' ever needs
   * remembering, even with several '*'s in one pattern: whatever
   * extending an earlier '*' further could recover, extending the
   * nearer one instead recovers equally well, since the nearer one
   * can simply absorb the same characters. This replaces both real
   * recursion and the previous per-candidate retry loop, neither of
   * which is needed anymore -- a stack-overflow segfault chasing a
   * Termux report (confirmed to reproduce identically under Linux, so
   * not platform-specific) traced back to the recursive version of
   * this function recursing once per character of a long string. */
  int have_star = 0;
  const char* star_pattern = NULL;
  size_t star_plen = 0;
  const char* star_string = NULL;
  size_t star_slen = 0;

/* label to jump back to instead of recursing */
start:
  /* when string is empty, only a pattern consisting of asteriks matches */
  if(slen == 0) {
    /* skip all asterikses */
    while(plen && *pattern == '*') {
      pattern++;
      plen--;
    }
    /* if there are chars left we don't have a match (which returns 1) */
    if(plen)
      goto fail;
    return 0;
  }
  /* there is still some string left but pattern ended */
  if(plen == 0)
    goto fail;
  /* if PATH_FNM_PERIOD is set, a leading period in string has to be
   * matched  exactly by a period in pattern.  A period is considered
   * to be leading if it is the first character in string, or if both
   * PATH_FNM_PATHNAME  is set and the period immediately follows a slash.
   */
  if(*string == '.' && *pattern != '.' && (flags & PATH_FNM_PERIOD) &&
     !(*pattern == '\\' && !(flags & PATH_FNM_NOESCAPE) && plen > 1 && pattern[1] == '.')) {
    /* don't match if PATH_FNM_PERIOD and this is the first char */
    if(!(flags & PATH_NOTFIRST))
      goto fail;
    /* don't match if PATH_FNM_PERIOD and PATH_FNM_PATHNAME and previous was '/'
     */
    if((flags & PATH_FNM_PATHNAME) && string[-1] == '/')
      goto fail;
  }
  flags |= PATH_NOTFIRST;

  switch(*pattern) {
    case '[': {
      const char* start;
      int neg = 0;

      if(!path_fnmatch_has_close(pattern, plen, flags))
        goto match;

      pattern++;
      plen--;
      /* unterminated character class because in a pathname the '/' is a
         separator and can't be matched. this means we have a mismatch */
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        goto fail;
      /* exclamation mark negates the class */
      neg = (*pattern == '!');
      pattern += neg;
      plen -= neg;
      /* now start scanning the pattern */
      start = pattern;

      while(plen) {
        int res = 0;
        int escaped = 0;

        /* a backslash-escaped member is always a literal char -- it
           can't close the bracket, start a range, or open a
           "[:class:]"/"[.symbol.]"/"[=equiv=]" construct either.
           POSIX bracket expressions don't define an escape mechanism
           of their own, but shish's own quote-removal reuses a real
           backslash as its internal "protect this literal char"
           marker regardless of whether the character ends up inside
           an open bracket expression or not (case "[.]" produces the
           same "\[.\]" text as case [\.]/[\]] do) -- so this parser
           has to honor it here too, or an escaped "]"/"[" inside a
           class is misread as really closing/opening one
           (case-quoted-bracket-not-literal, fixes/82). */
        if(*pattern == '\\' && plen > 1 && !(flags & PATH_FNM_NOESCAPE)) {
          escaped = 1;
          pattern++;
          plen--;
        }

        /* if there is a closing bracket and it's not
           the first char the class is terminated */
        if(!escaped && *pattern == ']' && pattern != start)
          break;

        if(!escaped && *pattern == '[' && plen > 1 && (pattern[1] == ':' || pattern[1] == '.' || pattern[1] == '=')) {
          /* MEMBER - "[:class:]" / "[.symbol.]" / "[=equiv=]" */
          char delim = pattern[1];
          size_t sublen = path_fnmatch_subexpr_len(pattern, plen, delim);

          if(sublen == 0) {
            /* unterminated -- treat the "[" itself as an ordinary
               literal member character, same as any other char
               falling through to the plain "else" branch below */
            res = (*pattern == *string);
            pattern++;
            plen--;
          } else if(delim == ':') {
            res = path_fnmatch_class(&pattern[2], sublen - 4, *string);
            pattern += sublen;
            plen -= sublen;
          } else {
            /* collating symbol / equivalence class: full locale-aware
               collation isn't implemented, so in the C/POSIX locale
               both degenerate to matching just the single enclosed
               character -- the only behavior a conforming
               application can rely on anyway. A multi-character
               symbol (no locale data to resolve it against) matches
               nothing rather than mismatching the bracket expression
               outright. */
            size_t symlen = sublen - 4;

            if(symlen == 1) {
              char rangeend;
              size_t rangeskip;

              /* "[.x.]-[.y.]" (or "[.x.]-y"): fuse into a real range
                 instead of matching "[.x.]", then "-", then "[.y.]"
                 as three separate members (fnmatch-bracket-collating-
                 range, fixes/83). */
              if(path_fnmatch_range_end(pattern + sublen, plen - sublen, &rangeend, &rangeskip)) {
                res = (*string >= pattern[2] && *string <= rangeend);
                sublen += rangeskip;
              } else {
                res = (pattern[2] == *string);
              }
            }

            pattern += sublen;
            plen -= sublen;
          }
        } else {
          /* MEMBER - character range (endpoint may be a plain char or
             a "[.symbol.]"/"[=equiv=]" single-character collating
             construct, e.g. the "-[.2.]" in "[a-[.2.]]") */
          char rangeend;
          size_t rangeskip;

          if(!escaped && path_fnmatch_range_end(pattern + 1, plen - 1, &rangeend, &rangeskip)) {
            /* check wheter char is within the specified range */
            res = (*string >= *pattern && *string <= rangeend);
            pattern += 1 + rangeskip;
            plen -= 1 + rangeskip;
          }
          /* MEMBER - literal character match */
          else {
            res = (*pattern == *string);
            pattern++;
            plen--;
          }
        }
        /* character class seems terminated and matched */
        if((res && !neg) || ((!res && neg) && *pattern == ']')) {
          while(plen && *pattern != ']') {
            /* skip an escaped char whole (backslash + the char it
               protects) so an escaped "]" further along the class
               doesn't end this scan early -- same escape rule as
               above, needed once matching has already succeeded and
               the rest of the class just needs skipping past. */
            if(*pattern == '\\' && plen > 1 && !(flags & PATH_FNM_NOESCAPE)) {
              pattern++;
              plen--;
            }
            /* "[:class:]"/"[.x.]"/"[=x=]" contains its own "]" */
            else if(*pattern == '[' && plen > 1 && (pattern[1] == ':' || pattern[1] == '.' || pattern[1] == '=')) {
              size_t sublen = path_fnmatch_subexpr_len(pattern, plen, pattern[1]);

              if(sublen) {
                pattern += sublen;
                plen -= sublen;
                continue;
              }
            }

            pattern++;
            plen--;
          }
          pattern += !!plen;
          plen -= !!plen;
          string++;
          slen--;
          goto start;
        }
        /* not terminated but unmatched */
        else if(res && neg)
          break;
      }
    } break;
    case '\\': {
      /* do we escape chars? */
      if(!(flags & PATH_FNM_NOESCAPE)) {
        /* escape next character... */
        pattern++;
        plen--;
        /* ...if there is one */
        if(plen)
          goto match;
      }
      /* don't escape -> literal match */
      else
        goto match;
    } break;
    case '*': {
      /* collapse a run of consecutive '*'s into one, then just record
       * this as the current backtrack bookmark (see the comment at
       * the top of the function) and continue matching the rest of
       * the pattern as if the '*' had consumed nothing yet -- if that
       * eventually fails, "fail:" below retries with this '*'
       * consuming one more character, until either something matches
       * or there's no more string left to give it. */
      pattern++;
      plen--;

      while(plen && *pattern == '*') {
        pattern++;
        plen--;
      }

      have_star = 1;
      star_pattern = pattern;
      star_plen = plen;
      star_string = string;
      star_slen = slen;
      goto start;
    }
    case '?': {
      /* it can't match a / when we're matching a pathname */
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        break;
      pattern++;
      plen--;
      string++;
      slen--;
    }
      goto start;
    default:
    match: {
      /* perform literal match */
      if(*pattern == *string) {
        pattern++;
        plen--;
        string++;
        slen--;
        goto start;
      }
    } break;
  }

fail:
  /* backtrack: let the most recently seen '*' consume one more
   * character and retry from right after it -- unless there's no
   * more string left to give it, or the next character is a '/' that
   * PATH_FNM_PATHNAME forbids a '*' from ever crossing. */
  if(have_star && star_slen && !(*star_string == '/' && (flags & PATH_FNM_PATHNAME))) {
    star_string++;
    star_slen--;
    pattern = star_pattern;
    plen = star_plen;
    string = star_string;
    slen = star_slen;
    goto start;
  }

  return PATH_FNM_NOMATCH;
}
