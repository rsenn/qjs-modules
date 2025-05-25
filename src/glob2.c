
#ifndef HAVE_GLOB

#include <stdlib.h>
#include <string.h>

#include "glob.h"
#include "path.h"
#include "char-utils.h"

static inline ssize_t
alloc_len(size_t len) {
  return ((len + (len >> 2) + 30) + 31) & (~(size_t)31);
}

static inline ssize_t
range_len(const my_range* range) {
  return range->end - range->start;
}

static my_range
range_dup(const char* s) {
  char* start = strdup(s);
  size_t len = strlen(s);
  return (my_range){start, start + len};
}

static char*
range_append(my_range* range, const void* x, size_t n) {
  size_t len = range_len(range);
  size_t o = alloc_len(len + 1);
  size_t a = alloc_len(len + n + 1);

  if(!range->start || a != o) {
    if(!(range->start = realloc(range->start, a)))
      return 0;
  }

  byte_copy(&range->start[len], n, x);
  range->end = range->start + len + n;

  return range->end;
}

static int
vec_push(my_vec* vec, const char* str) {
  if(vec->len + 1 > vec->res) {
    size_t res = alloc_len(vec->len + 1);

    if((vec->ptr = realloc(vec->ptr, res * sizeof(char*))))
      vec->res = res;
  }

  if(vec->ptr) {
    vec->ptr[vec->len] = strdup(str);
    vec->ptr[++vec->len] = 0;
    return 0;
  }

  return -1;
}

static int glob_components(char* rest, myglob_state* g);
static int glob_expand(char* pat, char* patend, myglob_state* g);

int
my_glob(const char* pattern, myglob_state* g) {
  g->pat = range_dup(pattern);
  g->buf = (my_range){0, 0};

  return glob_components(g->pat.start, g);
}

static int
glob_components(char* rest, myglob_state* g) {
  char *x = rest, *y = g->pat.end;

  while(x < y) {
    size_t offset = path_component2(x, y - x);
    size_t magic = byte_chrs(x, offset, "[?*{", 4);

    if(magic < offset)
      return glob_expand(x, x + offset, g);

    offset += path_separator2(x + offset, y - (x + offset));

    if(!range_append(&g->buf, x, offset))
      return -1;

    x += offset;
  }

  *g->buf.end = '\0';
  struct stat st;

  if(lstat(g->buf.start, &st) == -1)
    return -1;

  if((S_ISDIR(st.st_mode) || (S_ISLNK(st.st_mode) && (stat(g->buf.start, &st) == 0) && S_ISDIR(st.st_mode)))) {

    if(!(x = range_append(&g->buf, "/", 1)))
      return -1;

    *x = '\0';
  }

  return vec_push(&g->paths, g->buf.start);
}

static int
glob_expand(char* pat, char* patend, myglob_state* g) {
  Directory* dir;
  DirEntry* ent;
  int i = 0;

  dir = getdents_new();

  *g->buf.end = '\0';
  if(getdents_open(dir, g->buf.start))
    return -1;

  while((ent = getdents_read(dir))) {
    const char* name = getdents_cname(ent);

    if(path_isdot1(name) || path_isdotdot1(name))
      continue;

    size_t namelen = strlen(name);

    if(path_fnmatch5(pat, patend - pat, name, namelen, 0) != PATH_FNM_NOMATCH) {
      size_t sep, oldsize = range_len(&g->buf);
      range_append(&g->buf, name, namelen);

      if((sep = path_separator2(patend, g->pat.end - patend)))
        range_append(&g->buf, patend, sep);

      // if(patend == g->pat.end) printf("result: '%.*s' pat: '%.*s'\n", (int)range_len(&g->buf), g->buf.start, (int)(patend - pat), pat);

      glob_components(patend + sep, g);

      g->buf.end = g->buf.start + oldsize;
      *g->buf.end = '\0';
    }

    ++i;
  }

  getdents_close(dir);
  free(dir);
  return 0;
}

#endif /* HAVE_GLOB */
