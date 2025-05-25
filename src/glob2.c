
#ifndef HAVE_GLOB

#include <assert.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef _WIN32
#define lstat stat
#endif

#include "glob.h"
#include "path.h"
#include "char-utils.h"

/*static void
range_free(PointerRange* r) {
  if(r->start) {
    free(r->start);
    *r = range_null();
  }
}*/

static int
vec_push(struct vec* v, const char* str) {
  if(v->len + 1 > v->res) {
    uintptr_t res = alloc_len(v->len + 1);

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

static int glob_components(PointerRange, struct glob_state*);
static int glob_expand(PointerRange, struct glob_state*);
static int glob_tilde(char*, struct glob_state*);
static int glob_brace1(PointerRange, struct glob_state*);
static int glob_brace2(PointerRange, struct glob_state*);

int
my_glob(const char* pattern, struct glob_state* g) {
  g->pat = range_fromstr(pattern);
  g->buf = range_null();

  char *x = range_begin(&g->pat), *y = range_end(&g->pat);

  if(x < y && *x == '~') {
    int n;

    if((n = glob_tilde(x, g)))
      x += n;
  }

  size_t s;

  if((s = path_separator2(x, y - x))) {
    if(range_write(&g->buf, x, s))
      return -1;
    x += s;
  }

  if(g->flags & GLOB_BRACE)
    return glob_brace1((PointerRange){x, y}, g);
  else
    return glob_components((PointerRange){x, y}, g);
}

/**
 * \brief Brace globbing
 *
 * Expand recursively a glob {} pattern. When there is no more
 * expansion invoke the standard globbing routine to glob the
 * rest of the magic characters.
 *
 * @param {PointerRange}      pat   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_brace1(PointerRange pat, struct glob_state* g) {
  char* x = range_begin(&pat);
  char* const y = range_end(&pat);

  assert(!range_overlap(&g->buf, &pat));

  /* Protect a single {}, for find(1), like csh */
  if(x[0] == '{' && x[1] == '}' && x + 2 == y)
    return glob_components((PointerRange){x, y}, g);

  uintptr_t offset = byte_chr(x, y - x, '{');

  if(x + offset < y)
    return glob_brace2((PointerRange){x, x + offset}, g);

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
 * @param {PointerRange}      pat   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_brace2(PointerRange pat, struct glob_state* g) {
  int ret = 0, i;
  char *x = range_begin(&pat), *y = range_end(&pat);
  char *right = 0, *ptr = 0, *left = 0;
  PointerRange out = {0, 0};

  assert(!range_overlap(&g->buf, &pat));

  /* copy part up to the brace */
  if(range_append(&out, pat))
    return -1;
  if(range_append(&g->buf, pat))
    return -1;

  uintptr_t rl = range_len(&out);

  /* Find the balanced brace */
  for(i = 0, right = ++y; right < y; right++)
    if(*right == '[') {
      /* Ignore everything between [] */
      ptr = right++;
      right += byte_chr(right, y - right, ']');

      if(right == y)
        /* could not find a ptr ']' - ignore and just look for '}' */
        right = ptr;

    } else if(*right == '{') {
      i++;
    } else if(*right == '}') {
      if(i == 0)
        break;

      i--;
    }

  /* Non ptr braces; just glob the pattern */
  if(i != 0 || right == y)
    return glob_components((PointerRange){x, y}, g);

  for(i = 0, left = ptr = y; ptr <= right; ptr++)
    switch(*ptr) {
      case '[': {
        /* Ignore everything between [] */
        left = ptr++;
        ptr += byte_chr(ptr, y - ptr, ']');

        if(ptr == y)
          /* We could not find a ptr ']' - ignore and just look for '}' */
          ptr = left;

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
        if(!(i && *ptr == ',')) {

          /* Append the current string */
          if(range_append(&out, (PointerRange){left, ptr}))
            return -1;

          /* Append the rest of the pattern after the closing brace */
          if(range_append(&out, (PointerRange){right + 1, y}))
            return -1;

          /* Expand the current pattern */
          ret = glob_brace1((PointerRange){range_begin(&out) + rl, range_end(&out)}, g);

          if(range_resize(&out, rl))
            return -1;

          /* move after the comma, to the next string */
          left = ptr + 1;
        }

        break;
      }
    }

  return ret;
}

/**
 * @brief Tilde globbing
 *
 * @param {char*}              pattern  Pattern
 * @param {struct glob_state*} g        State
 *
 * @returns -1 on error
 */
static int
glob_tilde(char* pattern, struct glob_state* g) {
  char* const end = range_end(&g->pat);

  assert(!range_in(&g->buf, pattern));

  if(*pattern != '~' || !(g->flags & GLOB_TILDE))
    return 0;

  uintptr_t len = path_component2(pattern, end - pattern);
  uintptr_t slen = path_separator2(pattern + len, end - (pattern + len));

  if(len > 1) {
    char user[len];
    struct passwd* pw;

    byte_copy(user, len - 1, pattern + 1);
    user[len - 1] = '\0';

#ifdef _WIN32
#warning Get home directory for user
#else
    if(!(pw = getpwnam(user)) || !pw->pw_dir)
      return -1;

    if(range_puts(&g->buf, pw->pw_dir))
      return -1;
#endif
  } else {
    const char* home = path_gethome();

    if(range_puts(&g->buf, home))
      return -1;
  }

  if(range_write(&g->buf, pattern + len, slen))
    return -1;

  return len + slen;
}

/**
 * @brief Glob directory components
 *
 * @param {PointerRange}      rest   Pattern
 * @param {struct glob_state*} g     State
 *
 * @returns -1 on error
 */
static int
glob_components(PointerRange rest, struct glob_state* g) {
  char *x = range_begin(&rest), *y = range_end(&rest);

  assert(!range_overlap(&g->buf, &rest));

  while(x < y) {
    uintptr_t offset = path_component2(x, y - x);
    uintptr_t magic = byte_chrs(x, offset, "[?*{", 4);

    if(magic < offset)
      return glob_expand((PointerRange){x, x + offset}, g);

    offset += path_separator2(x + offset, y - (x + offset));

    if(range_write(&g->buf, x, offset))
      return -1;

    x += offset;
  }

  //*range_end(&g->buf) = '\0';
  struct stat st;
  char* z = range_begin(&g->buf);

  if(lstat(z, &st) == -1)
    return -1;

  if(S_ISDIR(st.st_mode)
#ifdef S_ISLNK
        || (S_ISLNK(st.st_mode) && (stat(z, &st) == 0) && S_ISDIR(st.st_mode))
#endif
      )
    if(range_puts(&g->buf, "/"))
      return -1;

  return vec_push(&g->paths, range_begin(&g->buf));
}

/**
 * @brief Expand filenames
 *
 * @param {PointerRange}      pat  Pattern
 * @param {struct glob_state*} g    State
 *
 * @returns -1 on error
 */
static int
glob_expand(PointerRange pat, struct glob_state* g) {
  Directory* dir;
  DirEntry* ent;
  int i = 0;
  char *x = range_begin(&pat), *y = range_end(&pat);

  assert(!range_overlap(&g->buf, &pat));

  dir = getdents_new();

  if(getdents_open(dir, range_str(&g->buf)))
    return -1;

  while((ent = getdents_read(dir))) {
    const char* name = getdents_cname(ent);

    if(path_isdot1(name) || path_isdotdot1(name))
      continue;

    uintptr_t namelen = strlen(name);

    if(path_fnmatch5(x, range_len(&pat), name, namelen, 0) != PATH_FNM_NOMATCH) {
      uintptr_t sep, oldsize = range_len(&g->buf);

      if(range_write(&g->buf, name, namelen))
        return -1;

      if((sep = path_separator2(y, range_end(&g->pat) - y)))
        if(range_write(&g->buf, y, sep))
          return -1;

      // if(y == range_begin(&g->pat).end) printf("result: '%.*s' x: '%.*s'\n",
      // (int)range_len(&g->buf), range_begin(&g->buf), (int)range_len(&pat), x);

      PointerRange rest = range_fromstr(y + sep);
      glob_components(rest, g);

      range_resize(&g->buf, oldsize);
      // *range_end(&g->buf) = '\0';
    }

    ++i;
  }

  getdents_close(dir);
  free(dir);
  return 0;
}

#endif /* HAVE_GLOB */
