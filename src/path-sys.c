/* path-sys.c: path_exists, path_isfile, path_issymlink, path_getcwd,
 * path_readlink, path_gethome, path_homeof.
 * depends on: path.h, buffer-utils.h (DynBuf).
 * rule: everything here asks the OS; the lexical code is path-lib.c. */

#include "char-utils.h"
#include "buffer-utils.h"
#include "path.h"
#include "utils.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

static const char path_passwd[] =
#ifdef __ANDROID__
    "/system/etc/passwd"
#else
    "/etc/passwd"
#endif
    ;

/* copies p[0..n) to a NUL-terminated stack buffer; returns 0 if too long. */
static int
path_cstr(char* buf, const char* p, size_t n) {
  if(n >= PATH_MAX)
    return 0;
  memcpy(buf, p, n);
  buf[n] = '\0';
  return 1;
}

/* true when lstat() succeeds on p[0..n); a dangling symlink exists. */
int
path_exists(const char* p, size_t n) {
  struct stat st;
  char buf[PATH_MAX];

  return path_cstr(buf, p, n) && lstat(buf, &st) == 0;
}

/* true when stat() says p[0..n) is a regular file (symlinks followed). */
int
path_isfile(const char* p, size_t n) {
  struct stat st;
  char buf[PATH_MAX];

  return path_cstr(buf, p, n) && stat(buf, &st) == 0 && S_ISREG(st.st_mode);
}

/* true when lstat() says p[0..n) is itself a symlink. */
int
path_issymlink(const char* p, size_t n) {
  char buf[PATH_MAX];

#ifdef _WIN32
  return path_cstr(buf, p, n) && is_symlink(buf);
#else
  struct stat st;

  return path_cstr(buf, p, n) && lstat(buf, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

/* stores the current directory in `db`; returns 1, or 0 on failure. */
int
path_getcwd(DynBuf* db) {
  dbuf_zero(db);
  dbuf_claim(db, PATH_MAX - db->size);

  if(getcwd((char*)db->buf, db->allocated_size) == NULL) {
    dbuf_zero(db);
    return 0;
  }

  db->size = strlen((const char*)db->buf);
  dbuf_0(db);
  return 1;
}

#define START ((PATH_MAX + 1) >> 7)

/* stores the target of the symlink `path` in `dir`; returns its length, or -1. */
int
path_readlink(const char* path, DynBuf* dir) {
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

/* home directory field of the first /etc/passwd line whose field `field`
 * (0 name, 2 uid) equals key[0..keylen); static buffer, NULL if none. */
static char*
path_passwd_home(int field, const char* key, size_t keylen) {
  static char home[PATH_MAX + 1];
  FILE* fp;
  char *line, *ret = 0, buf[1024];

  if(!(fp = fopen(path_passwd, "r")))
    return 0;

  while((line = fgets(buf, sizeof(buf), fp))) {
    char* f[7];
    size_t i, p, len = strlen(line);

    while(len > 0 && is_whitespace_char(buf[len - 1]))
      buf[--len] = '\0';

    for(i = 0; i < 7; i++) {
      f[i] = line;
      p = str_chr(line, ':');
      if(line[p] == '\0') {
        line += p;
        for(i++; i < 7; i++)
          f[i] = line;
        break;
      }
      line[p] = '\0';
      line += p + 1;
    }

    if(strlen(f[field]) != keylen || byte_diff(f[field], keylen, key))
      continue;

    strncpy(home, f[5], PATH_MAX);
    home[PATH_MAX] = '\0';
    ret = home;
    break;
  }

  fclose(fp);
  return ret;
}

/* home directory of user id `uid` from the passwd file, or NULL; static buffer. */
char*
path_gethome(int uid) {
  char id[24];
  size_t n = 0;
  unsigned u = uid < 0 ? 0 : uid;

  do
    id[n++] = '0' + u % 10;
  while((u /= 10));

  for(size_t i = 0; i < n / 2; i++) {
    char c = id[i];
    id[i] = id[n - 1 - i];
    id[n - 1 - i] = c;
  }

  return path_passwd_home(2, id, n);
}

/* home directory of the user named user[0..userlen), or NULL; static buffer. */
char*
path_homeof(const char* user, size_t userlen) {
  return path_passwd_home(0, user, userlen);
}

/* links followed before path_resolve gives up with ELOOP */
#define MAXSYMLINKS 40

#define IS_DOT(c, n) ((n) == 1 && (c)[0] == '.')
#define IS_DOTDOT(c, n) ((n) == 2 && (c)[0] == '.' && (c)[1] == '.')

/* length of the run of separators (sep != 0) or of non-separators at the
 * start (rev == 0) or at the end of p[0..n). */
static size_t
path_run(const char* p, size_t n, int sep, int rev) {
  size_t i = 0;

  if(rev)
    while(i < n && (path_issep(p[n - 1 - i]) != 0) == sep)
      ++i;
  else
    while(i < n && (path_issep(p[i]) != 0) == sep)
      ++i;

  return i;
}

/* drops the last component of db (below `root`) and the separator before it. */
static void
path_pop(DynBuf* db, size_t root) {
  size_t k = db->size - path_run((const char*)db->buf + root, db->size - root, 0, 1);

  if(k > root)
    k -= path_run((const char*)db->buf + root, k - root, 1, 1);

  db->size = k;
}

/* canonicalizes `path` into `db` (replaced, NUL-terminated): "." and ".."
 * resolved, symlinks followed unless `symbolic`, missing components kept.
 * returns 0 with errno set (ELOOP after MAXSYMLINKS), else 1 + links followed. */
static int
path_canonicalize(const char* path, DynBuf* db, int symbolic) {
  DynBuf rest, link, tmp;
  size_t pos = 0, root = 0, n;
  int ret = 0, links = 0;

  dbuf_init2(&rest, 0, 0);
  dbuf_init2(&link, 0, 0);
  dbuf_init2(&tmp, 0, 0);

  if(dbuf_put(&rest, (const uint8_t*)path, strlen(path)))
    goto fail;

  db->size = 0;

restart:
  if((root = path_rootlen((const char*)rest.buf, rest.size))) {
    if(dbuf_put(db, rest.buf, root))
      goto fail;

#ifndef _WIN32
    /* "//tmp" -> "/tmp": only Windows has a UNC root */
    if(root > 1)
      db->size = root = 1;
#endif
    db->buf[root - 1] = PATHSEP_C;
    pos = root;
  }

  while(pos < rest.size) {
    const char* c;

    if((pos += path_run((const char*)rest.buf + pos, rest.size - pos, 1, 0)) == rest.size)
      break;

    c = (const char*)rest.buf + pos;
    n = path_run(c, rest.size - pos, 0, 0);
    pos += n;

    if(IS_DOT(c, n))
      continue;

    if(IS_DOTDOT(c, n)) {
      size_t j = path_run((const char*)db->buf, db->size, 0, 1);

      /* relative with nothing to pop (or a ".." on top): keep the ".." */
      if(db->size > root && !IS_DOTDOT((const char*)db->buf + db->size - j, j))
        path_pop(db, root);
      else if(!root && ((db->size && dbuf_putc(db, PATHSEP_C)) || dbuf_put(db, (const uint8_t*)"..", 2)))
        goto fail;

      continue;
    }

    if((db->size > root && dbuf_putc(db, PATHSEP_C)) || dbuf_put(db, (const uint8_t*)c, n))
      goto fail;

    dbuf_0(db);

    if(symbolic || !path_issymlink((const char*)db->buf, db->size))
      continue;

    if(++links > MAXSYMLINKS) {
      errno = ELOOP;
      goto fail;
    }

    if(path_readlink((const char*)db->buf, &link) < 0)
      goto fail;

    /* the link's target followed by what is still to do is the new rest */
    path_pop(db, root);

    tmp.size = 0;

    if(dbuf_put(&tmp, link.buf, link.size) || dbuf_putc(&tmp, PATHSEP_C) || dbuf_put(&tmp, rest.buf + pos, rest.size - pos))
      goto fail;

    dbuf_0(&tmp);
    dbuf_free(&rest);
    rest = tmp;
    dbuf_init2(&tmp, 0, 0);
    pos = 0;

    /* absolute target: start over from its root */
    if(path_isabs((const char*)rest.buf, rest.size)) {
      db->size = 0;
      goto restart;
    }
  }

  if(db->size == 0 && dbuf_putc(db, '.'))
    goto fail;

  dbuf_0(db);
  ret = 1 + links;

fail:
  dbuf_free(&rest);
  dbuf_free(&link);
  dbuf_free(&tmp);
  return ret;
}

/* the absolute canonical form of p[0..n) in `db` (replaced, NUL-terminated).
 * a relative p is resolved against cwd[0..cwdn), or the current directory if
 * cwd is NULL or cwdn is 0.
 *
 *   symbolic != 0  purely lexical, symlinks are kept (realpath -s)
 *   symbolic == 0  every symlink is followed, a missing component is kept
 *
 * returns 0 with errno set (ELOOP after 40 links), else 1 + links followed.
 *
 *   "x/../y", cwd "/a" -> "/a/y"   "/usr/lib/../include/" -> "/usr/include"
 */
int
path_resolve(const char* p, size_t n, DynBuf* db, int symbolic, const char* cwd, size_t cwdn) {
  DynBuf own, full;
  size_t need;
  int ret = 0;

  dbuf_init2(&own, 0, 0);
  dbuf_init2(&full, 0, 0);

  if(!path_isabs(p, n)) {
    if(!cwd || !cwdn) {
      if(!path_getcwd(&own))
        goto end;

      cwd = (const char*)own.buf;
      cwdn = own.size;
    }
  } else {
    cwd = 0;
    cwdn = 0;
  }

  if(symbolic) {
    db->size = 0;
    need = path_abs(NULL, 0, p, n, cwd, cwdn);

    if(dbuf_claim(db, need + 1))
      goto end;

    db->size = path_abs((char*)db->buf, db->allocated_size, p, n, cwd, cwdn);
    dbuf_0(db);
    ret = 1;
  } else {
    need = path_cat(NULL, 0, cwd, cwdn, p, n);

    if(dbuf_claim(&full, need + 1))
      goto end;

    full.size = path_cat((char*)full.buf, full.allocated_size, cwd, cwdn, p, n);
    dbuf_0(&full);
    ret = path_canonicalize((const char*)full.buf, db, 0);
  }

end:
  dbuf_free(&own);
  dbuf_free(&full);
  return ret;
}
