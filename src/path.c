#include "path.h"
#include "utils.h"
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

static void path_normappend(const char* p, size_t n, DynBuf* db);

int
path_absolute3(const char* path, size_t len, DynBuf* db) {
  DynBuf tmp;
  int ret = 0;

  dbuf_init2(&tmp, 0, 0);

  if(!path_isabsolute2(path, len)) {
    if(path_getcwd1(&tmp) && tmp.size && len && tmp.buf[tmp.size - 1] != PATHSEP_C)
      dbuf_putc(&tmp, PATHSEP_C);

    ret = 1;
  }

  dbuf_put(&tmp, (const uint8_t*)path, len);

  if(tmp.size)
    path_normappend((const char*)tmp.buf, tmp.size, db);

  dbuf_0(db);
  dbuf_free(&tmp);

  return ret;
}

char*
path_absolute2(const char* path, size_t len) {
  DynBuf db;

  dbuf_init2(&db, 0, 0);
  path_absolute3(path, len, &db);
  dbuf_0(&db);

  return (char*)db.buf;
}

char*
path_absolute1(const char* path) {
  return path_absolute2(path, strlen(path));
}

void
path_append2(const char* x, DynBuf* db) {
  return path_append3(x, strlen(x), db);
}

void
path_append3(const char* x, size_t len, DynBuf* db) {
  if(db->size > 0 && db->buf[db->size - 1] != PATHSEP_C)
    dbuf_putc(db, PATHSEP_C);

  size_t pos = path_skipdotslash2(x, len);

  x += pos;
  len -= pos;

  dbuf_append(db, (const uint8_t*)x, len);
}

/* Node.js path.normalize(): resolves `.` and `..`, collapses repeated separators,
 * keeps a trailing separator; `..` above the root of an absolute path is dropped */
static void
path_normstr(const char* p, size_t n, int above, DynBuf* db) {
  size_t b0 = db->size, lastseg = 0, i;
  ssize_t lastslash = -1;
  int dots = 0, c = 0;

  for(i = 0; i <= n; i++) {
    if(i < n)
      c = (unsigned char)p[i];
    else if(path_issep(c))
      break;
    else
      c = PATHSEP_C;

    if(path_issep(c)) {
      size_t rl = db->size - b0;

      if(lastslash == (ssize_t)i - 1 || dots == 1) {
      } else if(dots == 2) {
        if(rl < 2 || lastseg != 2 || db->buf[db->size - 1] != '.' || db->buf[db->size - 2] != '.') {
          if(rl > 2) {
            size_t k = rl, k2;

            while(k > 0 && db->buf[b0 + k - 1] != PATHSEP_C)
              k--;

            if(k == 0) {
              db->size = b0;
              lastseg = 0;
            } else {
              rl = k - 1;
              db->size = b0 + rl;
              k2 = rl;
              while(k2 > 0 && db->buf[b0 + k2 - 1] != PATHSEP_C)
                k2--;
              lastseg = k2 == 0 ? rl : rl - k2;
            }

            lastslash = i;
            dots = 0;
            continue;
          } else if(rl != 0) {
            db->size = b0;
            lastseg = 0;
            lastslash = i;
            dots = 0;
            continue;
          }
        }

        if(above) {
          dbuf_putstr(db, rl > 0 ? PATHSEP_S ".." : "..");
          lastseg = 2;
        }
      } else {
        if(db->size > b0)
          dbuf_putc(db, PATHSEP_C);
        dbuf_put(db, (const uint8_t*)p + lastslash + 1, i - lastslash - 1);
        lastseg = i - lastslash - 1;
      }

      lastslash = i;
      dots = 0;
    } else if(c == '.' && dots != -1) {
      ++dots;
    } else {
      dots = -1;
    }
  }
}

/* appends the normalized form of `p` to `db`; `n` must be > 0 */
static void
path_normappend(const char* p, size_t n, DynBuf* db) {
  size_t b0 = db->size, b1;
  int abs = path_issep(p[0]), trailing = path_issep(p[n - 1]);

  if(abs)
    dbuf_putc(db, PATHSEP_C);

  b1 = db->size;
  path_normstr(p, n, !abs, db);

  if(db->size == b1) {
    if(!abs) {
      db->size = b0;
      dbuf_putstr(db, trailing ? "." PATHSEP_S : ".");
    }
  } else if(trailing) {
    dbuf_putc(db, PATHSEP_C);
  }
}

size_t
path_normalize3(const char* path, size_t n, DynBuf* db) {
  db->size = 0;

  if(n == 0)
    dbuf_putc(db, '.');
  else
    path_normappend(path, n, db);

  dbuf_0(db);

  return db->size;
}

size_t
path_normalize1(char* path) {
  return path_normalize2(path, strlen(path));
}

size_t
path_normalize2(char* path, size_t nb) {
  DynBuf tmp;
  size_t n;

  if(nb == 0)
    return 0;

  dbuf_init2(&tmp, 0, 0);
  path_normappend(path, nb, &tmp);

  n = MIN_NUM(tmp.size, nb);
  memcpy(path, tmp.buf, n);

  if(n < nb)
    path[n] = '\0';

  dbuf_free(&tmp);
  return n;
}

SizePair
path_common4(const char* s1, size_t n1, const char* s2, size_t n2) {
  SizePair r;

  for(r.sz1 = 0, r.sz2 = 0; r.sz1 != n1 && r.sz2 != n2;) {
    size_t i1, i2;
    i1 = path_separator2(&s1[r.sz1], n1 - r.sz1);
    i2 = path_separator2(&s2[r.sz2], n2 - r.sz2);

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

/* Node.js path.extname(): ignores trailing separators, `.`/`..` and leading dots have no extension */
static void
path_ext(const char* p, size_t n, size_t* pos, size_t* len) {
  ssize_t startdot = -1, startpart = 0, end = -1, i;
  int matched = 1, predot = 0;

  for(i = n - 1; i >= 0; --i) {
    int c = p[i];

    if(path_issep(c)) {
      if(!matched) {
        startpart = i + 1;
        break;
      }
      continue;
    }

    if(end == -1) {
      matched = 0;
      end = i + 1;
    }

    if(c == '.') {
      if(startdot == -1)
        startdot = i;
      else if(predot != 1)
        predot = 1;
    } else if(startdot != -1) {
      predot = -1;
    }
  }

  if(startdot == -1 || end == -1 || predot == 0 || (predot == 1 && startdot == end - 1 && startdot == startpart + 1)) {
    *pos = n;
    *len = 0;
  } else {
    *pos = startdot;
    *len = end - startdot;
  }
}

const char*
path_extname1(const char* p) {
  return p + path_extpos1(p);
}

size_t
path_extpos1(const char* p) {
  size_t pos, len;

  path_ext(p, strlen(p), &pos, &len);
  return pos;
}

size_t
path_extlen1(const char* p) {
  size_t pos, len;

  path_ext(p, strlen(p), &pos, &len);
  return len;
}

char*
path_search(const char** path_ptr, const char* name, DynBuf* db) {
  size_t n;
  const char* path = *path_ptr;

  if(*path == '\0')
    return 0;

  n = str_chr(path, PATHDELIM_S[0]);

  db->size = 0;
  dbuf_put(db, (const uint8_t*)path, n);
  dbuf_putc(db, PATHSEP_C);
  dbuf_putstr(db, name);
  dbuf_0(db);

  if(path[n])
    ++n;

  *path_ptr += n;

  return (char*)db->buf;
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
  path_absolute3(path, len, &db);
  dbuf_0(&db);
  ret = path_resolve3((const char*)db.buf, buf, 1);
  dbuf_free(&db);
  dbuf_0(buf);

  return ret;
}

int
path_relative3(const char* path, const char* relative_to, DynBuf* out) {
  return path_relative5(path, strlen(path), relative_to, strlen(relative_to), out);
}

char*
path_relative1(const char* path) {
  char *rel = 0, *cwd;

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
  DynBuf fb, tb;
  const char *from, *to;
  size_t fn, tn, fromlen, tolen, length, i;
  ssize_t lastsep = -1, k;

  dbuf_zero(out);
  dbuf_init2(&fb, 0, 0);
  dbuf_init2(&tb, 0, 0);

  /* like path.resolve(): absolute, normalized, no trailing separator */
  path_absolute3(s2, n2, &fb);
  path_absolute3(s1, n1, &tb);

  fn = fb.size;
  tn = tb.size;
  from = (const char*)fb.buf;
  to = (const char*)tb.buf;

  while(fn > 1 && path_issep(from[fn - 1]))
    fn--;
  while(tn > 1 && path_issep(to[tn - 1]))
    tn--;

  if(fn == tn && !memcmp(from, to, fn))
    goto end;

  fromlen = fn - 1;
  tolen = tn - 1;
  length = MIN_NUM(fromlen, tolen);

  for(i = 0; i < length; i++) {
    if(from[1 + i] != to[1 + i])
      break;
    else if(path_issep(from[1 + i]))
      lastsep = i;
  }

  if(i == length) {
    if(tolen > length) {
      if(path_issep(to[1 + i])) {
        dbuf_put(out, (const uint8_t*)to + 1 + i + 1, tolen - i - 1);
        goto end;
      }

      if(i == 0) {
        dbuf_put(out, (const uint8_t*)to + 1, tolen);
        goto end;
      }
    } else if(fromlen > length) {
      if(path_issep(from[1 + i]))
        lastsep = i;
      else if(i == 0)
        lastsep = 0;
    }
  }

  for(k = 1 + lastsep + 1; k <= (ssize_t)fn; ++k)
    if(k == (ssize_t)fn || path_issep(from[k]))
      dbuf_putstr(out, out->size == 0 ? ".." : PATHSEP_S "..");

  dbuf_put(out, (const uint8_t*)to + 1 + lastsep, tn - 1 - lastsep);

end:
  dbuf_0(out);
  dbuf_free(&fb);
  dbuf_free(&tb);
  return 1;
}

size_t
path_root2(const char* x, size_t n) {
  if(n > 0 && x[0] == PATHSEP_C)
    return 1;

  if(n >= 3 && isalnum(x[0]) && x[1] == ':' && path_issep(x[2]))
    return 3;

  return 0;
}

/* Node.js path.dirname(): length of the directory part of `path`, 0 for `.` */
size_t
path_dirlen2(const char* path, size_t n) {
  int hasroot, matched = 1;
  ssize_t end = -1, i;

  if(n == 0)
    return 0;

  hasroot = path_issep(path[0]);

  for(i = n - 1; i >= 1; --i) {
    if(path_issep(path[i])) {
      if(!matched) {
        end = i;
        break;
      }
    } else {
      matched = 0;
    }
  }

  if(end == -1)
    return hasroot ? 1 : 0;

  if(hasroot && end == 1)
    return 2;

  return end;
}

size_t
path_dirlen1(const char* path) {
  return path_dirlen2(path, strlen(path));
}

char*
path_dirname1(const char* path) {
  size_t i;

  if((i = path_dirlen1(path)))
    return path_dup2(path, i);

  return path_dup1(".");
}

size_t
path_basename1(const char* path) {
  size_t len;

  return path_basename3(path, &len, strlen(path));
}

/* Node.js path.basename() (without suffix): trailing separators are ignored,
 * empty for `''` and `/` */
size_t
path_basename3(const char* path, size_t* len, size_t n) {
  ssize_t start = 0, end = -1, i;
  int matched = 1;

  for(i = n - 1; i >= 0; --i) {
    if(path_issep(path[i])) {
      if(!matched) {
        start = i + 1;
        break;
      }
    } else if(end == -1) {
      matched = 0;
      end = i + 1;
    }
  }

  if(end == -1) {
    *len = 0;
    return n;
  }

  *len = end - start;
  return start;
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
