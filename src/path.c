#include "char-utils.h"
#include "buffer-utils.h"
#include "path.h"
#include "utils.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

#ifdef _WIN32
#include <shlobj.h>
#endif

static const char path_passwd[] =
#ifdef __ANDROID__
    "/system/etc/passwd"
#else
    "/etc/passwd"
#endif
    ;

/**
 * \addtogroup path
 * @{
 */

char*
path_dup3(const char* path, size_t n, DynBuf* db) {
  size_t len = MIN_NUM(n, strlen(path));

  dbuf_claim(db, len + 1 - db->size);
  memcpy(db->buf, path, len);
  db->buf[len] = '\0';
  return (char*)db->buf;
}

char*
path_dup2(const char* path, size_t n) {
  DynBuf db;
  dbuf_init2(&db, 0, 0);
  path_dup3(path, n, &db);
  return (char*)db.buf;
}

size_t
path_components3(const char* p, size_t len, uint32_t n) {
  const char *s = p, *e = p + len;
  size_t count = 0;

  while(s < e) {
    s += path_separator2(s, e - s);

    if(s == e)
      break;

    s += path_component3(s, e - s, 0);

    if(--n <= 0)
      break;

    count++;
  }

  return count;
}

const char*
path_at4(const char* p, size_t plen, size_t* len_ptr, int i) {
  size_t next, len = 0;
  const char* q;

  for(q = p + plen; p < q;) {
    len = path_component3(p, q - p, 0);
    next = len + path_separator2(&p[len], q - p - len);

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
  size_t plen = path_length2(p, len);
  size_t dlen = path_length2(dir, dirlen);

  if(plen < dlen)
    return 0;

  for(i = 0; i < dlen; i++) {
    size_t pcomplen, dcomplen;
    const char *q, *pdir;
    q = path_at4(p, len, &pcomplen, i);
    pdir = path_at4(dir, dirlen, &dcomplen, i);

    if(!(pcomplen == dcomplen && !strncmp(q, pdir, pcomplen)))
      return 0;
  }

  return 1;
}

int
path_diff4(const char* a, size_t la, const char* b, size_t lb) {
  size_t aindex = 0, bindex = 0, alen = path_length2(a, la), blen = path_length2(b, lb);
  int ret = 0;

  while(aindex < alen && bindex < blen) {
    size_t an, bn;
    const char *p, *q;

    do
      p = path_at4(a, la, &an, aindex++);
    while(an == 1 && *p == '.');

    do
      q = path_at4(b, lb, &bn, bindex++);
    while(bn == 1 && *q == '.');

    if(an != bn)
      return an - bn;

    if((ret = strncmp(p, q, an)))
      return ret;
  }

  if(aindex < alen)
    return alen - aindex;
  if(bindex < blen)
    return -(blen - bindex);

  return 0;
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

      break;
    }
    case '\\': {
      if(!(flags & PATH_FNM_NOESCAPE)) {
        pattern++;
        plen--;

        if(plen)
          goto match;
      } else
        goto match;

      break;
    }
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

      goto start;
    }

    default:
    match: {
      if(*pattern == *string) {
        pattern++;
        plen--;
        string++;
        slen--;
        goto start;
      }

      break;
    }
  }

  return PATH_FNM_NOMATCH;
}

char*
path_getcwd1(DynBuf* db) {
  dbuf_zero(db);
  dbuf_claim(db, PATH_MAX - db->size);

  if(getcwd((char*)db->buf, db->allocated_size) == NULL) {
    dbuf_zero(db);
    return 0;
  }

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
#if defined(_WIN32)
  static char home[PATH_MAX + 1];

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
  char *line, *ret = 0, buf[1024];

  if((fp = fopen(path_passwd, "r"))) {
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
      byte_copy(home, n, dir);
      home[n] = '\0';
      ret = home;
    }

    fclose(fp);
  }

  return ret;
}

char*
path_gethome2(const char* user, size_t userlen) {
  static char home[PATH_MAX + 1];
  FILE* fp;
  char *line, *ret = 0, buf[1024];

  if((fp = fopen(path_passwd, "r"))) {
    while((line = fgets(buf, sizeof(buf) - 1, fp))) {
      size_t p, n, len = strlen(line);
      char* dir;

      while(len > 0 && is_whitespace_char(buf[len - 1]))
        buf[--len] = '\0';

      p = str_chr(buf, ':');

      if(p != userlen || byte_diff(buf, userlen, user))
        continue;

      line = buf + p + 1;

      for(n = 4; n > 0; n--) {
        p = str_chr(line, ':');
        line[p] = '\0';
        line += p + 1;
      }

      dir = line;
      n = str_chr(line, ':');
      byte_copy(home, n, dir);
      home[n] = '\0';
      ret = home;
    }

    fclose(fp);
  }

  return ret;
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

        if(!(rret = path_resolve3(buf, db, symbolic)))
          return 0;
      }
    }
  }

  if(db->size == 0)
    dbuf_putc(db, sep);

  return ret;
}

int
path_realpath3(const char* path, size_t len, DynBuf* buf) {
  int ret;
  DynBuf db;

  if(!path_exists2(path, len))
    return 0;

  dbuf_init2(&db, 0, 0);

  {
    DynBuf cwd;
    size_t need;

    dbuf_init2(&cwd, 0, 0);
    path_getcwd1(&cwd);
    need = path_abs(NULL, 0, path, len, (const char*)cwd.buf, cwd.size);

    if(!dbuf_claim(&db, need + 1)) {
      db.size = path_abs((char*)db.buf, db.allocated_size, path, len, (const char*)cwd.buf, cwd.size);
      dbuf_0(&db);
    }

    dbuf_free(&cwd);
  }

  ret = path_resolve3((const char*)db.buf, buf, 1);
  dbuf_free(&db);
  dbuf_0(buf);

  return ret;
}

#define START ((PATH_MAX + 1) >> 7)

int
path_readlink2(const char* path, DynBuf* dir) {
  ssize_t n = (START ? START : 32);
  ssize_t sz;

  do {
    n <<= 1;
    dbuf_claim(dir, n - dir->size);

    if((sz = readlink(path, (char*)dir->buf, n)) == -1)
      return -1;
  } while(sz == n);

  dir->size = sz;
  return dir->size;
}

/**
 * @}
 */
