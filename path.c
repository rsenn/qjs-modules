#include "path.h"
#include "buffer-utils.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

/**
 * \addtogroup path
 * @{
 */

int
path_absolute(const char* path, DynBuf* db) {
  if(!path_isabs(path)) {
    dbuf_realloc(db, PATH_MAX + 1);
    if(getcwd((char*)db->buf, PATH_MAX + 1))
      db->size = strlen((const char*)db->buf);
    else
      db->size = 0;
    if(strcmp(path, ".")) {
      dbuf_putc(db, PATHSEP_C);
      dbuf_putstr(db, path);
    }
    return 1;
  } else {
    dbuf_putstr(db, path);
  }
  return 0;
}

int
path_absolute_db(DynBuf* db) {
  int ret = 0;
  dbuf_putc(db, '\0');
  db->size--;

  if(!path_is_absolute((const char*)db->buf, db->size)) {
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
path_append(const char* x, size_t len, DynBuf* db) {
  if(db->size > 0 && db->buf[db->size - 1] != PATHSEP_C)
    dbuf_putc(db, PATHSEP_C);

  if(len > 2 && x[0] == '.' && x[1] == PATHSEP_C) {
    x += 2;
    len -= 2;
  }
  dbuf_append(db, (const uint8_t*)x, len);
}

int
path_canonical(const char* path, DynBuf* db) {
  db->size = 0;
  dbuf_putstr(db, path);
  dbuf_putc(db, '\0');
  db->size--;
  return path_canonical_buf(db);
}

int
path_canonical_buf(DynBuf* db) {
  db->size = path_collapse((char*)db->buf, db->size);
  dbuf_putc(db, '\0');
  db->size--;
  return 1;
}

size_t
path_collapse(char* path, size_t n) {
  char* x;
  int ret = 0;
  char sep = path_getsep(path);
  size_t l, i;

  for(x = path, i = 0; i < n;) {
    while(x[i] == sep) i++;

    l = i + byte_chr(&x[i], n - i, sep);
    if(l < n) {
      l++;
      if(l + 2 <= n) {
        if(x[l] == '.' && x[l + 1] == '.' && (l + 2 >= n || x[l + 2] == sep)) {
          l += 3;
          if(l < n)
            memmove(&x[i], &x[l], n - l);
          n = i + (n - l);
          x[n] = '\0';

          while(x[--i] == sep)
            ;
          while(i > 0 && x[i] != sep) i--;
          continue;
        }
      }
    }

    i = l;
  }

  return n;
}

SizePair
path_common_prefix(const char* s1, size_t n1, const char* s2, size_t n2) {
  SizePair r;

  for(r.sz1 = 0, r.sz2 = 0; r.sz1 != n1 && r.sz2 != n2;) {
    size_t i1, i2;

    i1 = path_skip_separator(&s1[r.sz1], n1 - r.sz1, 0);
    i2 = path_skip_separator(&s2[r.sz2], n2 - r.sz2, 0);

    if(!!i1 != !!i2)
      break;

    r.sz1 += i1;
    r.sz2 += i2;

    i1 = path_skip_component(&s1[r.sz1], n1 - r.sz1, 0);
    i2 = path_skip_component(&s2[r.sz2], n2 - r.sz2, 0);

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
path_components(const char* p, size_t len, uint32_t n) {
  const char *s = p, *e = p + len;
  size_t count = 0;
  while(s < e) {
    s += path_skip_separator(s, e - s, 0);
    if(s == e)
      break;

    s += path_length(s, e - s);
    if(--n <= 0)
      break;

    count++;
  }
  return count;
}

void
path_concat(const char* a, size_t alen, const char* b, size_t blen, DynBuf* db) {
  DynBuf tmp;
  const char* x;
  size_t size;
  dbuf_init(&tmp);

  path_append(a, alen, &tmp);
  path_append(b, blen, &tmp);

  x = (const char*)tmp.buf;
  size = tmp.size;
  if(size > 2 && tmp.buf[0] == '.' && tmp.buf[1] == PATHSEP_C) {
    x += 2;
    size -= 2;
  }
  dbuf_append(db, (const uint8_t*)x, size);
  dbuf_putc(db, '\0');
  db->size--;

  dbuf_free(&tmp);
}

int
path_exists(const char* p) {
  struct stat st;
  int r;
  return ((r = lstat(p, &st)) == 0);
}

const char*
path_extname(const char* p) {
  size_t pos;
  char* q;
  if((q = strrchr(p, PATHSEP_C)))
    p = q + 1;

  pos = str_rchr(p, '.');
  p += pos;
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
path_fnmatch(const char* pattern, unsigned int plen, const char* string, unsigned int slen, int flags) {
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
      if((*string == '/' && (flags & PATH_FNM_PATHNAME)) || path_fnmatch(pattern, plen, string + 1, slen - 1, flags)) {
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
path_getcwd(DynBuf* db) {
  dbuf_zero(db);
  dbuf_realloc(db, PATH_MAX);
  getcwd((char*)db->buf, db->allocated_size);
  db->size = strlen((const char*)db->buf);
  dbuf_0(db);
  return (char*)db->buf;
}

char*
path_gethome(int uid) {
  FILE* fp;

  char *line, *ret = 0;
  char buf[1024];
  static char home[PATH_MAX + 1];
  if((fp = fopen("/etc/passwd", "r")) == 0)
    return 0;
  while((line = fgets(buf, sizeof(buf) - 1, fp))) {
    size_t p, n, len = strlen(line);
    char *user, *id, *dir;
    while(len > 0 && is_whitespace_char(buf[len - 1])) buf[--len] = '\0';
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
  return ret;
}

int
path_is_absolute(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;
#ifdef _WIN32
  if(n >= 2 && x[1] == ':')
    return 1;
#endif
  return 0;
}

int
path_is_directory(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISDIR(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_file(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISREG(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_chardev(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISCHR(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_blockdev(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISBLK(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_fifo(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISFIFO(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_socket(const char* p) {
  struct stat st;
  int r;
  if((r = stat(p, &st) == 0)) {
    if(S_ISSOCK(st.st_mode))
      return 1;
  }
  return 0;
}

int
path_is_symlink(const char* p) {
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
path_normalize(const char* path, DynBuf* db, int symbolic) {
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
    while(path_issep(*path)) sep = *path++;
    if(path[0] == '.') {
      if(path_issep(path[1]) || path[1] == '\0') {
        path++;
        continue;
      }
      if(path[1] == '.' && (path_issep(path[2]) || path[2] == '\0')) {
        db->size = path_right((const char*)db->buf, db->size);
        path += 2;
        continue;
      }
    }
    if(*path == '\0')
      break;
    if(db->size && (db->buf[db->size - 1] != '/' && db->buf[db->size - 1] != '\\'))
      dbuf_putc(db, sep);
    n = path_length_s(path);
    dbuf_append(db, (const uint8_t*)path, n);
    if(n == 2 && path[1] == ':')
      dbuf_putc(db, sep);
    dbuf_0(db);
    path += n;
    memset(&st, 0, sizeof(st));
    if(stat_fn((const char*)db->buf, &st) != -1 && path_is_symlink((const char*)db->buf)) {
      ret++;
      if((ssize_t)(n = readlink((const char*)db->buf, buf, PATH_MAX)) == (ssize_t)-1)
        return 0;
      if(path_is_absolute(buf, n)) {
        strncpy(&buf[n], path, PATH_MAX - n);
        dbuf_zero(db);
        dbuf_putc(db, sep);
        path = buf;
        goto start;
      } else {
        int rret;
        db->size = path_right((const char*)db->buf, db->size);
        buf[n] = '\0';
        rret = path_normalize(buf, db, symbolic);
        if(!rret)
          return 0;
      }
    }
  }
  if(db->size == 0)
    dbuf_putc(db, sep);
  return ret;
}

int
path_relative(const char* path, const char* relative_to, DynBuf* out) {
  return path_relative_b(path, strlen(path), relative_to, strlen(relative_to), out);
}

int
path_relative_b(const char* s1, size_t n1, const char* s2, size_t n2, DynBuf* out) {
  SizePair p;
  size_t i;

  p = path_common_prefix(s1, n1, s2, n2);
  dbuf_zero(out);

  s1 += p.sz1;
  n1 -= p.sz1;
  s2 += p.sz2;
  n2 -= p.sz2;

  while((i = path_skip(s2, n2))) {
    dbuf_putstr(out, ".." PATHSEP_S);
    s2 += i;
    n2 -= i;
  }

  i = path_skip_separator(s1, n1, 0);
  dbuf_append(out, s1 + i, n1 - i);

  if(out->size == 0)
    dbuf_putc(out, '.');
  else if(out->buf[out->size - 1] == PATHSEP_C)
    out->size--;

  dbuf_0(out);
  return 1;
}

size_t
path_root(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;

  if(n >= 3 && isalnum(x[0]) && x[1] == ':' && path_issep(x[2]))
    return 3;

  return 0;
}

size_t
path_skip_component(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;
  if(pos > len)
    pos = len;

  p += pos;
  while(p < end && !path_issep(*p)) ++p;
  return p - start;
}

size_t
path_skip_separator(const char* p, size_t len, size_t pos) {
  const char *start = p, *end = p + len;
  if(pos > len)
    pos = len;

  p += pos;
  while(p < end && path_issep(*p)) ++p;
  return p - start;
}

char*
__path_dirname(const char* path, DynBuf* dir) {
  size_t i = str_rchrs(path, "/\\", 2);
  if(path == NULL || path[i] == '\0') {
    dbuf_putstr(dir, ".");
  } else {
    /* remove trailing slashes */
    while(i > 0 && path_issep(path[i - 1])) --i;
    dbuf_put(dir, (const uint8_t*)path, i);
  }
  dbuf_0(dir);
  return (char*)dir->buf;
}

char*
path_dirname(const char* path) {
  DynBuf dir;
  dbuf_init2(&dir, 0, 0);
  return __path_dirname(path, &dir);
}

#define START ((PATH_MAX + 1) >> 7)

int
path_readlink(const char* path, DynBuf* dir) {
  /* do not allocate PATH_MAX from the beginning,
     most paths will be smaller */
  ssize_t n = (START ? START : 32);
  ssize_t sz;
  do {
    /* reserve some space */
    n <<= 1;
    dbuf_realloc(dir, n);
    if((sz = readlink(path, (char*)dir->buf, n)) == -1)
      return -1;
    /* repeat until we have reserved enough space */
  } while(sz == n);

  dir->size = sz;
  /* now truncate to effective length */
  return dir->size;
}

/**
 * @}
 */
