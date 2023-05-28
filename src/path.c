#include "path.h"
#include "buffer-utils.h"
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef HAVE_LSTAT
#define lstat stat
#endif

#ifdef _WIN32
#include <shlobj.h>
#endif

static int
path_canonical_buf(DynBuf* db) {
  db->size = path_normalize2((char*)db->buf, db->size);
  dbuf_putc(db, '\0');
  db->size--;
  return 1;
}

static char*
__path_dirname(const char* path, DynBuf* dir) {
  size_t i = str_rchrs(path, "/\\", 2);
  if(path == NULL || path[i] == '\0') {
    dbuf_putstr(dir, ".");
  } else {
    while(i > 0 && path_issep(path[i - 1]))
      --i;
    dbuf_put(dir, (const uint8_t*)path, i);
  }

  dbuf_0(dir);
  return (char*)dir->buf;
}

/*static char*
path_dirname_alloc(const char* path) {
  DynBuf dir;
  dbuf_init2(&dir, 0, 0);
  return __path_dirname(path, &dir);
}*/

/**
 * \addtogroup path
 * @{
 */
/*char*
path_dup2(const char* path, DynBuf* db) {
  return path_dup3(path,strlen(path),db);
}*/

char*
path_dup3(const char* path, size_t n, DynBuf* db) {
  size_t len = MIN_NUM(n, strlen(path));

  dbuf_realloc(db, len + 1);
  memcpy(db->buf, path, len);
  db->buf[len] = '\0';
  return (char*)db->buf;
}

char*
path_dup1(const char* path) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_dup3(path, strlen(path), &db);
  return (char*)db.buf;
}

char*
path_dup2(const char* path, size_t n) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_dup3(path, n, &db);
  return (char*)db.buf;
}

int
path_absolute3(const char* path, size_t len, DynBuf* db) {
  if(!path_isabsolute2(path, len)) {
    dbuf_realloc(db, PATH_MAX + 1);
    if(getcwd((char*)db->buf, PATH_MAX + 1))
      db->size = strlen((const char*)db->buf);
    else
      db->size = 0;
    if(strncmp(path, ".", len)) {
      dbuf_putc(db, PATHSEP_C);
      dbuf_putstr(db, path);
    }

    return 1;
  } else {
    dbuf_putstr(db, path);
  }

  return 0;
}

char*
path_absolute2(const char* path, size_t len) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);

  /* if(db.buf)*/ {
    path_absolute3(path, len, &db);
    dbuf_0(&db);
  }
  return (char*)db.buf;
}

char*
path_absolute1(const char* path) {
  return path_absolute2(path, strlen(path));
}

static int
path_absolute_db(DynBuf* db) {
  int ret = 0;
  dbuf_putc(db, '\0');
  db->size--;
  if(!path_isabsolute2((const char*)db->buf, db->size)) {
    DynBuf tmp;
    dbuf_init(&tmp);
    dbuf_append(&tmp, db->buf, db->size);
    dbuf_realloc(db, PATH_MAX + 1);
    if(getcwd((char*)db->buf, PATH_MAX)) {
      db->size = strlen((const char*)db->buf);
    }

    dbuf_putc(db, PATHSEP_C);
    dbuf_append(db, (const uint8_t*)tmp.buf, tmp.size);
    dbuf_free(&tmp);
    ret = 1;
  }

  if(db->size && path_issep(db->buf[db->size - 1]))
    --db->size;
  dbuf_putc(db, '\0');
  db->size--;
  return ret;
}

void
path_append3(const char* x, size_t len, DynBuf* db) {
  if(db->size > 0 && db->buf[db->size - 1] != PATHSEP_C)
    dbuf_putc(db, PATHSEP_C);
  dbuf_append(db, (const uint8_t*)x, len);
}

int
path_canonical3(const char* path, size_t len, DynBuf* db) {
  db->size = 0;
  dbuf_put(db, (const uint8_t*)path, len);
  dbuf_putc(db, '\0');
  db->size--;
  return path_canonical_buf(db);
}

char*
path_canonical2(const char* path, size_t len) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_canonical3(path, len, &db);
  return (char*)db.buf;
}

char*
path_canonical1(const char* path) {
  return path_canonical2(path, strlen(path));
}

size_t
path_normalize3(const char* path, size_t n, DynBuf* db) {
  size_t ret;
  dbuf_realloc(db, n + 1);
  memcpy(db->buf, path, n);
  db->buf[n] = '\0';
  ret = path_normalize2((char*)db->buf, n);
  return ret;
}

size_t
path_normalize1(char* path) {
  return path_normalize2(path, strlen(path));
}

size_t
path_normalize2(char* path, size_t nb) {
  ssize_t i = 0, j, len;
  len = nb;

again:
  i = path_separator3(path, len, 0);

  while(i < len) {
    j = i + path_skip2(&path[i], len - i);
    if(j == i) {
      len = j;
      break;
    }

    if(i > 0 && path_isdot(&path[i])) {
      ssize_t clen = len - j;
      assert(clen >= 0);
      if(clen > 0)
        byte_copy(&path[i], clen, &path[j]);
      path[i + clen] = '\0';
      len = i + clen;
      goto again;
    }

    if(!path_isdotdot(&path[i]) && (len - j) >= 2 && path_isdotdot(&path[j])) {
      j += (len - j) == 2 || path[j + 2] == '\0' ? 2 : 3;
      ssize_t clen = len - j;
      assert(clen >= 0);
      if(clen > 0)
        byte_copy(&path[i], clen, &path[j]);
      path[i + clen] = '\0';
      len = i + clen;
      goto again;
    }

    i = j;
  }

  return len;
}

SizePair
path_common4(const char* s1, size_t n1, const char* s2, size_t n2) {
  SizePair r;

  for(r.sz1 = 0, r.sz2 = 0; r.sz1 != n1 && r.sz2 != n2;) {
    size_t i1, i2;
    i1 = path_separator3(&s1[r.sz1], n1 - r.sz1, 0);
    i2 = path_separator3(&s2[r.sz2], n2 - r.sz2, 0);
    if(!!i1 != !!i2)
      break;
    r.sz1 += i1;
    r.sz2 += i2;
    i1 = path_component3(&s1[r.sz1], n1 - r.sz1, 0);
    i2 = path_component3(&s2[r.sz2], n2 - r.sz2, 0);
    if(i1 != i2)
      break;
    if(memcmp(&s1[r.sz1], &s2[r.sz2], i1))
      break;
    r.sz1 += i1;
    r.sz2 += i2;
  }

  return r;
}

size_t
path_components3(const char* p, size_t len, uint32_t n) {
  const char *s = p, *e = p + len;
  size_t count = 0;
  while(s < e) {
    s += path_separator3(s, e - s, 0);
    if(s == e)
      break;
    s += path_component3(s, e - s, 0);
    if(--n <= 0)
      break;
    count++;
  }

  return count;
}

void
path_concat5(const char* a, size_t alen, const char* b, size_t blen, DynBuf* db) {
  path_append3(a, alen, db);
  while(blen >= 1 && path_isdot(b)) {
    size_t n;

    if(!(n = path_skipdotslash2(b, blen)))
      break;

    b += n;
    blen -= n;
  }

  path_append3(b, blen, db);
  dbuf_0(db);
}

char*
path_concat4(const char* a, size_t alen, const char* b, size_t blen) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);

  if(db.buf) {
    path_concat5(a, alen, b, blen, &db);
    dbuf_0(&db);
  }
  return (char*)db.buf;
}

void
path_concat3(const char* a, const char* b, DynBuf* db) {
  path_append3(a, strlen(a), db);
  while(*b && path_isdot(b)) {
    size_t n = path_skip1(b);
    b += n;
  }

  path_append3(b, strlen(b), db);
  dbuf_0(db);
}

char*
path_concat2(const char* a, const char* b) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);

  if(db.buf) {
    path_concat3(a, b, &db);
    dbuf_0(&db);
  }
  return (char*)db.buf;
}

const char*
path_at4(const char* p, size_t plen, size_t* len_ptr, int i) {
  int pos = 0;
  size_t next, len;
  const char* q = p + plen;
  for(; p < q;) {
    len = path_component3(p, q - p, 0);
    next = len + path_separator3(&p[len], q - p - len, 0);
    if(i <= 0)
      break;
    p += next;
    --i;
  }

  if(len_ptr)
    *len_ptr = len;
  return p;
}

const char*
path_at3(const char* p, size_t* len_ptr, int i) {
  int pos = 0;
  size_t next, len;
  for(;;) {
    len = path_component1(p);
    next = len + path_separator1(&p[len]);
    if(i <= 0)
      break;
    p += next;
    --i;
  }

  if(len_ptr)
    *len_ptr = len;
  return p;
}

size_t
path_offset4(const char* p, size_t len, size_t* len_ptr, int i) {
  const char* q;

  if((q = path_at4(p, len, len_ptr, i)) >= p)
    return q - p;

  return 0;
}

size_t
path_offset3(const char* p, size_t* len_ptr, int i) {
  const char* q;

  if((q = path_at3(p, len_ptr, i)) >= p)
    return q - p;

  return 0;
}

size_t
path_offset2(const char* p, int i) {
  return path_offset3(p, NULL, i);
}

size_t
path_size2(const char* p, int i) {
  size_t len;
  path_offset3(p, &len, i);
  return len;
}

const char*
path_at2(const char* p, int i) {
  return path_at3(p, NULL, i);
}

size_t
path_length1(const char* p) {
  return path_length2(p, strlen(p));
}

size_t
path_length2(const char* p, size_t slen) {
  int pos = 0;
  size_t next, len;
  const char* end = p + slen;

  for(; p < end; p += next) {
    len = path_component1(p);
    next = len + path_separator1(&p[len]);

    if(pos && len == 0)
      break;

    ++pos;
  }
  return pos;
}

int
path_slice4(const char* p, int start, int end, DynBuf* db) {
  int i;
  size_t next, len;
  size_t n = db->size;

  for(i = 0; i < end; i++) {
    len = path_component1(p);
    next = len + path_separator1(&p[len]);

    if(i >= start) {
      if((db->size > n && db->buf[db->size - 1] != PATHSEP_C) || (i == start && len == 0))
        dbuf_putc(db, PATHSEP_C);

      dbuf_put(db, (const uint8_t*)p, len);
    }

    p += next;
  }
  return i;
}

char*
path_slice3(const char* p, int start, int end) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);

  if(db.buf) {
    path_slice4(p, start, end, &db);
    dbuf_0(&db);
  }
  return (char*)db.buf;
}

/*
size_t
path_size1(const char* p) {
  size_t n = 0, len;
  for(; *p; p += len) {
    len = path_skip1(p);
    ++n;
  }

  return n;
}

size_t
path_size2(const char* p, size_t n) {
  size_t len;
  const char* q = p + n;
  for(; p < q; p += len) {
    len = path_skip2(p, q - p);
    ++n;
  }

  return n;
}*/

int
path_exists1(const char* p) {
  struct stat st;
  int r;
  return ((r = lstat(p, &st)) == 0);
}

int
path_exists2(const char* p, size_t len) {
  char* q;
  int ret = 0;

  if((q = path_dup2(p, len))) {
    ret = path_exists1(q);
    free(q);
  }
  return ret;
}

int
path_isin4(const char* p, size_t len, const char* dir, size_t dirlen) {
  size_t i;

  if(len < dirlen)
    return 0;

  for(i = 0; i < dirlen; i++) {
    size_t plen, dlen;
    const char *q, *pdir;
    q = path_at4(p, len, &plen, i);
    pdir = path_at4(dir, dirlen, &dlen, i);

    if(!(plen == dlen && !strncmp(q, pdir, plen)))
      return 0;
  }
  return 1;
}

int
path_isin2(const char* p, const char* dir) {
  size_t i, len = path_length1(dir);

  if(path_length1(p) < len)
    return 0;

  for(i = 0; i < len; i++) {
    size_t plen, dlen;
    const char *q, *pdir;
    q = path_at3(p, &plen, i);
    pdir = path_at3(dir, &dlen, i);

    if(!(plen == dlen && !strncmp(q, pdir, plen)))
      return 0;
  }
  return 1;
}

int
path_equal4(const char* a, size_t la, const char* b, size_t lb) {
  size_t i, alen = path_length2(a, la), blen = path_length2(b, lb);

  if(alen != blen)
    return 0;

  for(i = 0; i < alen; i++) {
    size_t an, bn;
    const char *p, *q;
    p = path_at4(a, la, &an, i);
    q = path_at4(b, lb, &bn, i);

    if(an != bn)
      return 0;

    if(strncmp(p, q, an))
      return 0;
  }
  return 1;
}

int
path_equal2(const char* a, const char* b) {
  return path_equal4(a, strlen(a), b, strlen(b));
}

const char*
path_extname1(const char* p) {
  size_t pos;
  char* q;
  if((q = strrchr(p, PATHSEP_C)))
    p = q + 1;
  pos = str_rchr(p, '.');
  p += pos ? pos : strlen(p);
  return p;
}

int
path_find(const char* path, const char* name, DynBuf* db) {
  DIR* dir;
  struct dirent* entry;
  int ret = 0;

  if(!(dir = opendir(path)))
    return 0;

  while((entry = readdir(dir))) {
    const char* s = entry->d_name;
    if(!strcasecmp(s, name)) {
      dbuf_putstr(db, path);
      dbuf_putc(db, PATHSEP_C);
      dbuf_putstr(db, s);
      ret = 1;
      break;
    }
  }

  closedir(dir);
  return ret;
}

int
path_fnmatch5(const char* pattern, size_t plen, const char* string, size_t slen, int flags) {
start:
  if(slen == 0) {
    while(plen && *pattern == '*') {
      pattern++;
      plen--;
    }

    return (plen ? PATH_FNM_NOMATCH : 0);
  }

  if(plen == 0)
    return PATH_FNM_NOMATCH;
  if(*string == '.' && *pattern != '.' && (flags & PATH_FNM_PERIOD)) {
    if(!(flags & PATH_NOTFIRST))
      return PATH_FNM_NOMATCH;
    if((flags & PATH_FNM_PATHNAME) && string[-1] == '/')
      return PATH_FNM_NOMATCH;
  }

  flags |= PATH_NOTFIRST;
  switch(*pattern) {
    case '[': {
      const char* start;
      int neg = 0;
      pattern++;
      plen--;
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        return PATH_FNM_NOMATCH;
      neg = (*pattern == '!');
      pattern += neg;
      plen -= neg;
      start = pattern;
      while(plen) {
        int res = 0;
        if(*pattern == ']' && pattern != start)
          break;
        if(*pattern == '[' && pattern[1] == ':') {
        } else {
          if(plen > 1 && pattern[1] == '-' && pattern[2] != ']') {
            res = (*string >= *pattern && *string <= pattern[2]);
            pattern += 3;
            plen -= 3;
          } else {
            res = (*pattern == *string);
            pattern++;
            plen--;
          }
        }

        if((res && !neg) || ((!res && neg) && *pattern == ']')) {
          while(plen && *pattern != ']') {
            pattern++;
            plen--;
          }

          pattern += !!plen;
          plen -= !!plen;
          string++;
          slen--;
          goto start;
        } else if(res && neg)
          break;
      }

    } break;
    case '\\': {
      if(!(flags & PATH_FNM_NOESCAPE)) {
        pattern++;
        plen--;
        if(plen)
          goto match;
      } else
        goto match;
    } break;
    case '*': {
      if((*string == '/' && (flags & PATH_FNM_PATHNAME)) || path_fnmatch5(pattern, plen, string + 1, slen - 1, flags)) {
        pattern++;
        plen--;
        goto start;
      }

      return 0;
    }

    case '?': {
      if(*string == '/' && (flags & PATH_FNM_PATHNAME))
        break;
      pattern++;
      plen--;
      string++;
      slen--;
    }

      goto start;
    default:
    match : {
      if(*pattern == *string) {
        pattern++;
        plen--;
        string++;
        slen--;
        goto start;
      }

    } break;
  }

  return PATH_FNM_NOMATCH;
}

char*
path_getcwd1(DynBuf* db) {
  dbuf_zero(db);
  dbuf_realloc(db, PATH_MAX);
  getcwd((char*)db->buf, db->allocated_size);
  db->size = strlen((const char*)db->buf);
  dbuf_0(db);
  return (char*)db->buf;
}

char*
path_getcwd0(void) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_getcwd1(&db);
  return (char*)db.buf;
}

char*
path_gethome(void) {
  static char home[PATH_MAX + 1];
#if defined(_WIN32)
  if(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) != S_OK)
    return 0;
  return home;
#else
  return getenv("HOME");
#endif
}

char*
path_gethome1(int uid) {
  static char home[PATH_MAX + 1];
  FILE* fp;
  char *line, *ret = 0;
  char buf[1024];
  if((fp = fopen("/etc/passwd", "r"))) {
    while((line = fgets(buf, sizeof(buf) - 1, fp))) {
      size_t p, n, len = strlen(line);
      char *user, *id, *dir;
      while(len > 0 && is_whitespace_char(buf[len - 1]))
        buf[--len] = '\0';
      user = buf;
      user[p = str_chr(user, ':')] = '\0';
      line = buf + p + 1;
      for(n = 1; n > 0; n--) {
        p = str_chr(line, ':');
        line[p] = '\0';
        line += p + 1;
      }

      id = line;
      for(n = 3; n > 0; n--) {
        p = str_chr(line, ':');
        line[p] = '\0';
        line += p + 1;
      }

      if(atoi(id) != uid)
        continue;
      dir = line;
      n = str_chr(line, ':');
      strncpy(home, dir, n);
      home[n] = '\0';
      ret = home;
    }

    fclose(fp);
  }
  return ret;
}

int
path_isabsolute2(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
#ifdef _WIN32
  if(n >= 2 && x[1] == ':')
    return 1;
#endif
  return 0;
}

int
path_isabsolute1(const char* x) {
  if(x[0] && x[0] == PATHSEP_C)
    return 1;
#ifdef _WIN32
  if(x[0] && x[1] && x[1] == ':')
    return 1;
#endif
  return 0;
}

int
path_stat2(const char* p, size_t plen, struct stat* st) {
  char* q;
  int r = 0;
  if((q = path_dup2(p, plen))) {
    r = stat(q, st);
    free(q);
  }
  return r;
}

int
path_isdir1(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0))
    r = S_ISDIR(st.st_mode);
  return r;
}

int
path_isdir2(const char* p, size_t plen) {
  struct stat st;
  int r = 0;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISDIR(st.st_mode);
  return r;
}

int
path_isfile1(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISREG(st.st_mode))
      return 1;
  }

  return 0;
}

int
path_isfile2(const char* p, size_t plen) {
  struct stat st;
  int r;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISREG(st.st_mode);
  return r;
}

int
path_ischardev1(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISCHR(st.st_mode))
      return 1;
  }

  return 0;
}

int
path_ischardev2(const char* p, size_t plen) {
  struct stat st;
  int r;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISCHR(st.st_mode);
  return r;
}

int
path_isblockdev1(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISBLK(st.st_mode))
      return 1;
  }

  return 0;
}

int
path_isblockdev2(const char* p, size_t plen) {
  struct stat st;
  int r;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISBLK(st.st_mode);
  return r;
}

int
path_isfifo1(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISFIFO(st.st_mode))
      return 1;
  }

  return 0;
}

int
path_isfifo2(const char* p, size_t plen) {
  struct stat st;
  int r;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISFIFO(st.st_mode);
  return r;
}

int
path_issocket1(const char* p) {
#ifdef S_ISSOCK
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISSOCK(st.st_mode))
      return 1;
  }

#endif
  return 0;
}

int
path_issocket2(const char* p, size_t plen) {
#ifdef S_ISSOCK
  struct stat st;
  int r;
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISSOCK(st.st_mode);
  return r;
#endif
  return 0;
}

int
path_issymlink1(const char* p) {
#ifdef _WIN32
  return is_symlink(p);
#else
  struct stat st;
  int r;
  if((r = lstat(p, &st) == 0)) {
    if(S_ISLNK(st.st_mode))
      return 1;
  }

  return 0;
#endif
}

int
path_issymlink2(const char* p, size_t plen) {
  struct stat st;
  int r = 0;
#ifdef S_ISLNK
  if((r = path_stat2(p, plen, &st) == 0))
    r = S_ISLNK(st.st_mode);
#endif
  return r;
}

int
path_resolve3(const char* path, DynBuf* db, int symbolic) {
  size_t n;
  struct stat st;
  int ret = 1;
  char sep, buf[PATH_MAX + 1];
  int (*stat_fn)(const char*, struct stat*) = stat;
  if(symbolic)
    stat_fn = lstat;

  if(path_issep(*path)) {
    dbuf_putc(db, (sep = *path));
    path++;
  } else
    sep = PATHSEP_C;

start:

  while(*path) {
    while(path_issep(*path))
      sep = *path++;
    if(path[0] == '.') {
      if(path_issep(path[1]) || path[1] == '\0') {
        path++;
        continue;
      }
      /*
            if(path[1] == '.' && (path_issep(path[2]) || path[2] == '\0')) {
              if(db->size && db->buf)
                db->size = path_right2((const char*)db->buf, db->size);
              path += 2;
              continue;
            }*/
    }

    if(*path == '\0')
      break;
    if(db->size && (db->buf[db->size - 1] != '/' && db->buf[db->size - 1] != '\\'))
      dbuf_putc(db, sep);
    n = path_component3(path, strlen(path), 0);
    dbuf_append(db, (const uint8_t*)path, n);
    if(n == 2 && path[1] == ':')
      dbuf_putc(db, sep);
    dbuf_0(db);
    path += n;
    memset(&st, 0, sizeof(st));
    if(stat_fn((const char*)db->buf, &st) != -1 && path_issymlink1((const char*)db->buf)) {
      ret++;
      if((ssize_t)(n = readlink((const char*)db->buf, buf, PATH_MAX)) == (ssize_t)-1)
        return 0;
      if(path_isabsolute2(buf, n)) {
        strncpy(&buf[n], path, PATH_MAX - n);
        dbuf_zero(db);
        dbuf_putc(db, sep);
        path = buf;
        goto start;
      } else {
        int rret;
        db->size = path_right2((const char*)db->buf, db->size);
        buf[n] = '\0';
        rret = path_resolve3(buf, db, symbolic);
        if(!rret)
          return 0;
      }
    }
  }

  if(db->size == 0)
    dbuf_putc(db, sep);
  return ret;
}

char*
path_resolve2(const char* path, int symbolic) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_resolve3(path, &db, symbolic);
  dbuf_0(&db);
  return (char*)db.buf;
}

int
path_realpath3(const char* path, size_t len, DynBuf* buf) {
  int ret;
  DynBuf db;
  if(!path_exists2(path, len))
    return 0;
  dbuf_init2(&db, 0, 0);
  path_absolute3(path, len, &db);
  dbuf_0(&db);
  ret = path_resolve3((const char*)db.buf, buf, 1);
  dbuf_free(&db);
  dbuf_0(buf);
  return ret;
}

char*
path_realpath2(const char* path, size_t len) {
  char* ret;
  DynBuf db;
  if(!path_exists2(path, len))
    return 0;
  dbuf_init2(&db, 0, 0);
  path_absolute3(path, len, &db);
  dbuf_0(&db);
  ret = path_resolve2((const char*)db.buf, 1);
  dbuf_free(&db);
  return ret;
}

char*
path_realpath1(const char* path) {
  return path_realpath2(path, strlen(path));
}

int
path_relative3(const char* path, const char* relative_to, DynBuf* out) {
  return path_relative5(path, strlen(path), relative_to, strlen(relative_to), out);
}

char*
path_relative1(const char* path) {
  char *rel, *cwd;

  if((cwd = path_getcwd0())) {
    rel = path_relative2(path, cwd);
    free(cwd);
  }
  return rel;
}

char*
path_relative2(const char* path, const char* relative_to) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_relative3(path, relative_to, &db);
  return (char*)db.buf;
}

int
path_relative5(const char* s1, size_t n1, const char* s2, size_t n2, DynBuf* out) {
  SizePair p;
  size_t i;

  p = path_common4(s1, n1, s2, n2);
  dbuf_zero(out);
  s1 += p.sz1;
  n1 -= p.sz1;
  s2 += p.sz2;
  n2 -= p.sz2;

  i = path_separator3(s2, n2, 0);
  s2 += i;
  n2 -= i;

  while((i = path_skip2(s2, n2))) {
    dbuf_putstr(out, ".." PATHSEP_S);
    s2 += i;
    n2 -= i;
  }

  i = path_separator3(s1, n1, 0);
  dbuf_append(out, s1 + i, n1 - i);

  if(out->size == 0)
    dbuf_putc(out, '.');
  else if(out->buf[out->size - 1] == PATHSEP_C)
    out->size--;

  dbuf_0(out);
  return 1;
}

char*
path_relative4(const char* s1, size_t n1, const char* s2, size_t n2) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_relative5(s1, n1, s2, n2, &db);
  return (char*)db.buf;
}

size_t
path_root2(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
  if(n >= 3 && isalnum(x[0]) && x[1] == ':' && path_issep(x[2]))
    return 3;
  return 0;
}

size_t
path_component3(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;
  if(pos > len)
    pos = len;
  p += pos;
  while(p < end && !path_issep(*p))
    ++p;
  return p - start;
}

size_t
path_component1(const char* p) {
  const char* s = p;
  while(*s && !path_issep(*s))
    ++s;
  return s - p;
}

size_t
path_separator3(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;
  if(pos > len)
    pos = len;
  p += pos;
  while(p < end && path_issep(*p))
    ++p;
  return p - start;
}

size_t
path_separator1(const char* p) {
  const char* s = p;
  while(*s && path_issep(*s))
    ++s;
  return s - p;
}

static inline size_t
__path_dirname1(const char* path) {
  return str_rchrs(path, PATHSEP_S, 2);
}

static inline size_t
__path_dirname2(const char* path, size_t n) {
  return byte_rchrs(path, n, PATHSEP_S, 2);
}

char*
path_dirname1(const char* path) {
  size_t i;

  if(path[(i = __path_dirname1(path))])
    return path_dup2(path, i);

  return path_dup1(".");
}

char*
path_dirname2(const char* path, size_t n) {
  size_t i;

  if((i = __path_dirname2(path, n)) < n)
    return path_dup2(path, i);

  return path_dup1(".");
}

char*
path_dirname3(const char* path, size_t n, char* dest) {
  size_t i;

  if((i = byte_rchrs(path, n, PATHSEP_S, 2)) < n) {
    memcpy(dest, path, i);
    dest[i] = '\0';

    return dest;
  }
  return 0;
}

size_t
path_dirlen1(const char* path) {
  size_t i = str_rchrs(path, "/\\", 2);
  if(path[i] == '\0')
    return 0;
  while(i > 0 && path_issep(path[i - 1]))
    --i;
  return i;
}

size_t
path_dirlen2(const char* path, size_t n) {
  size_t i;
  if((i = byte_rchrs(path, n, "/\\", 2)) == n)
    return 0;
  while(i > 0 && path_issep(path[i - 1]))
    --i;
  return i;
}

#define START ((PATH_MAX + 1) >> 7)

int
path_readlink2(const char* path, DynBuf* dir) {
  ssize_t n = (START ? START : 32);
  ssize_t sz;
  do {
    n <<= 1;
    dbuf_realloc(dir, n);
    if((sz = readlink(path, (char*)dir->buf, n)) == -1)
      return -1;
  } while(sz == n);

  dir->size = sz;
  return dir->size;
}

char*
path_readlink1(const char* path) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_readlink2(path, &db);
  return (char*)db.buf;
}

int
path_compare4(const char* a, size_t alen, const char* b, size_t blen) {
  if(alen < blen)
    return -1;
  if(blen > alen)
    return 1;
  return strncmp(a, b, alen);
}

/**
 * @}
 */
