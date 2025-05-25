
#ifndef HAVE_GLOB

#include <assert.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include "glob.h"
#include "path.h"
#include "char-utils.h"

static inline ssize_t
alloc_len(size_t len) {
  return ((len + (len >> 2) + 30) + 31) & (~(size_t)31);
}

static inline int
in_range(const struct range* r, char* ptr) {
  return ptr >= r->start && ptr <= r->end;
}

static inline int
overlap(const struct range* a, const struct range* b) {
  return in_range(a, b->start) || in_range(a, b->end);
}

static inline ssize_t
range_len(const struct range* r) {
  return r->end - r->start;
}

static struct range
range_dup(const char* s) {
  char* start = strdup(s);
  size_t len = strlen(s);
  return (struct range){start, start + len};
}

static void
range_free(struct range* r) {
  if(r->start) {
    free(r->start);
    *r = (struct range){0, 0};
  }
}

static struct range
range_fromstr(const char* s) {
  size_t len = strlen(s);
  return (struct range){(char*)s, (char*)s + len};
}

static int
range_resize(struct range* r, size_t newlen) {
  size_t len = range_len(r);

  if(newlen > len) {
    size_t res = alloc_len(len + 1);

    if(newlen < res)
      return -1;
  }

  r->end = r->start + newlen;
  return 0;
}

static char*
range_write(struct range* r, const void* x, size_t n) {
  size_t len = range_len(r);
  size_t o = alloc_len(len + 1);
  size_t a = alloc_len(len + n + 1);

  if(!r->start || a != o) {
    if(!(r->start = realloc(r->start, a)))
      return 0;
  }

  byte_copy(&r->start[len], n, x);
  r->end = r->start + len + n;

  *r->end = '\0';

  return r->end;
}

static inline char*
range_append(struct range* r, struct range other) {
  return range_write(r, other.start, range_len(&other));
}

static int
vec_push(struct vec* v, const char* str) {
  if(v->len + 1 > v->res) {
    size_t res = alloc_len(v->len + 1);

    if((v->ptr = realloc(v->ptr, res * sizeof(char*))))
      v->res = res;
  }

  if(v->ptr) {
    v->ptr[v->len] = strdup(str);
    v->ptr[++v->len] = 0;
    return 0;
  }

  return -1;
}

static int glob_components(struct range, struct glob_state*);
static int glob_expand(struct range, struct glob_state*);
static int glob_tilde(char*, struct glob_state*);
static int glob_brace1(struct range, struct glob_state*);
static int glob_brace2(struct range, struct glob_state*);

int
my_glob(const char* pattern, struct glob_state* g) {
  g->pat = range_fromstr(pattern);
  g->buf = (struct range){0, 0};

  char* x = g->pat.start;

  if(x < g->pat.end && *x == '~') {
    int n;

    if((n = glob_tilde(x, g)))
      x += n;
  }

  if(g->flags & GLOB_BRACE)
    return glob_brace1((struct range){x, g->pat.end}, g);
  else
    return glob_components((struct range){x, g->pat.end}, g);
}

/**
 * \brief Brace globbing
 *
 * Expand recursively a glob {} pattern. When there is no more
 * expansion invoke the standard globbing routine to glob the
 * rest of the magic characters.
 *
 * @param {struct range}      pat   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_brace1(struct range pat, struct glob_state* g) {
  char* x = pat.start;
  char* const y = pat.end;

  assert(!overlap(&g->buf, &pat));

  /* Protect a single {}, for find(1), like csh */
  if(x[0] == '{' && x[1] == '}' && x + 2 == y)
    return glob_components((struct range){x, y}, g);

  size_t offset = byte_chr(x, y - x, '{');

  if(x + offset < y)
    return glob_brace2((struct range){x, x + offset}, g);

  return glob_components(pat, g);
}

/**
 * @brief Recursive brace globbing helper.
 *
 * Tries to expand a single brace.
 * If it succeeds then it invokes globexp1 with the new pattern.
 * If it fails then it tries to glob the rest of the pattern and
 * returns.
 *
 * @param {struct range}      pat   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_brace2(struct range pat, struct glob_state* g) {
  int ret = 0, i;
  char* lm;
  char* const y = g->pat.end;
  const char *pe, *pm, *pl;
  struct range out = {0, 0};

  assert(!overlap(&g->buf, &pat));

  /* copy part up to the brace */
  lm = range_append(&out, pat);

  size_t rl = range_len(&out);
  /*for(lm = patbuf, pm = pat.start; pm != pat.end; *lm++ = *pm++)
    continue;

  ls = lm;*/

  /* Find the balanced brace */
  for(i = 0, pe = ++pat.end; pe < y; pe++)
    if(*pe == '[') {
      /* Ignore everything between [] */
      for(pm = pe++; pe < y && *pe != ']'; pe++)
        continue;

      if(pe == y) {
        /* could not find a matching ']'
         - ignore and just look for '}' */
        pe = pm;
      }
    } else if(*pe == '{') {
      i++;
    } else if(*pe == '}') {
      if(i == 0)
        break;

      i--;
    }

  /* Non matching braces; just glob the pattern */
  if(i != 0 || pe == y) {
    return glob_components((struct range){pat.start, y}, g);
  }

  for(i = 0, pl = pm = pat.end; pm <= pe; pm++)
    switch(*pm) {
      case '[': {
        /* Ignore everything between [] */
        for(pl = pm++; pm < y && *pm != ']'; pm++)
          continue;

        if(pm == y) {
          /*
           * We could not find a matching ']'.
           * Ignore and just look for '}'
           */
          pm = pl;
        }

        break;
      }
      case '{': {
        i++;
        break;
      }
      case '}': {
        if(i) {
          i--;
          break;
        }
      }
        /* FALLTHROUGH */
      case ',': {
        if(i && *pm == ',')
          break;

        /* Append the current string */
        range_append(&out, (struct range){pl, pm});

        /*for(lm = out.start + rl; (pl < pm); *lm++ = *pl++) continue;*/

        /* Append the rest of the pattern after the closing brace */
        if(!(lm = range_append(&out, (struct range){pe + 1, y})))
          return -1;

        /*for(pl = pe + 1; pl <= y && (*lm++ = *pl++);) continue;*/

        *lm = '\0';

        /* Expand the current pattern */
        ret = glob_brace1((struct range){out.start, lm}, g);

        if(range_resize(&out, rl))
          return -1;

        /* move after the comma, to the next string */
        pl = pm + 1;

        break;
      }
        /*default: { break; }*/
    }

  return ret;
}

/**
 * @brief Tilde globbing
 *
 * @param {char*}         pattern Pattern
 * @param {struct glob_state*} g       State
 *
 * @returns -1 on error
 */
static int
glob_tilde(char* pattern, struct glob_state* g) {
  assert(!in_range(&g->buf, pattern));

  if(*pattern != '~' || !(g->flags & GLOB_TILDE))
    return 0;

  size_t len = path_component2(pattern, g->pat.end - pattern);
  size_t slen = path_separator2(pattern + len, g->pat.end - (pattern + len));

  if(len > 1) {
    char user[len];
    struct passwd* pw;

    byte_copy(user, len - 1, pattern + 1);
    user[len - 1] = '\0';

    if(!(pw = getpwnam(user)) || !pw->pw_dir)
      return -1;

    range_write(&g->buf, pw->pw_dir, strlen(pw->pw_dir));
  } else {
    const char* home = path_gethome();

    range_write(&g->buf, home, strlen(home));
  }

  range_write(&g->buf, pattern + len, slen);
  return len + slen;
}

/**
 * @brief Glob directory components
 *
 * @param {struct range}      rest   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_components(struct range rest, struct glob_state* g) {
  char *x = rest.start, *y = rest.end;

  assert(!overlap(&g->buf, &rest));

  if(x < y && path_isabsolute2(x, y - x)) {
    size_t n = path_separator2(x, y - x);

    if(!range_write(&g->buf, x, n))
      return -1;
    x += n;
  }

  while(x < y) {
    size_t offset = path_component2(x, y - x);
    size_t magic = byte_chrs(x, offset, "[?*{", 4);

    if(magic < offset)
      return glob_expand((struct range){x, x + offset}, g);

    offset += path_separator2(x + offset, y - (x + offset));

    if(!range_write(&g->buf, x, offset))
      return -1;

    x += offset;
  }

  *g->buf.end = '\0';
  struct stat st;

  if(lstat(g->buf.start, &st) == -1)
    return -1;

  if((S_ISDIR(st.st_mode) || (S_ISLNK(st.st_mode) && (stat(g->buf.start, &st) == 0) && S_ISDIR(st.st_mode)))) {

    if(!(x = range_write(&g->buf, "/", 1)))
      return -1;

    *x = '\0';
  }

  return vec_push(&g->paths, g->buf.start);
}

/**
 * @brief Expand filenames
 *
 * @param {struct range}      pat  Pattern
 * @param {struct glob_state*} g    State
 *
 * @returns -1 on error
 */
static int
glob_expand(struct range pat, struct glob_state* g) {
  Directory* dir;
  DirEntry* ent;
  int i = 0;

  assert(!overlap(&g->buf, &pat));

  dir = getdents_new();

  *g->buf.end = '\0';

  if(getdents_open(dir, g->buf.start))
    return -1;

  while((ent = getdents_read(dir))) {
    const char* name = getdents_cname(ent);

    if(path_isdot1(name) || path_isdotdot1(name))
      continue;

    size_t namelen = strlen(name);

    if(path_fnmatch5(pat.start, range_len(&pat), name, namelen, 0) != PATH_FNM_NOMATCH) {
      size_t sep, oldsize = range_len(&g->buf);
      range_write(&g->buf, name, namelen);

      if((sep = path_separator2(pat.end, g->pat.end - pat.end)))
        range_write(&g->buf, pat.end, sep);

      // if(pat.end == g->pat.start.end) printf("result: '%.*s' pat.start: '%.*s'\n", (int)range_len(&g->buf),
      // g->buf.start, (int)range_len(&pat), pat.start);

      glob_components(range_fromstr(pat.end + sep), g);

      range_resize(&g->buf, oldsize);
      *g->buf.end = '\0';
    }

    ++i;
  }

  getdents_close(dir);
  free(dir);
  return 0;
}

#endif /* HAVE_GLOB */
