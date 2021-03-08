#define _GNU_SOURCE

#include "path.h"
#include <stdio.h>

int
path_absolute(const char* path, DynBuf* db) {
  int ret = 0;
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

  if(!path_isabs((const char*)db->buf)) {
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

size_t
path_collapse(char* path, size_t n) {
  char* x = path;
  int ret = 0;
  char sep = path_getsep(path);
  char* end = x + n;
  while(x < end) {
    size_t i = byte_chr(x, end - x, sep);
    if(x + i < end) {
      i++;
      if(x + i + 2 < end) {
        if(x[i] == '.' && x[i + 1] == '.' && (x + i + 2 == end || x[i + 2] == sep)) {
          i += 3;
          memcpy(x, &x[i], n - i);
          end -= i;
          ret++;
          continue;
        }
      }
    }
    x += i;
    n -= i;
  }
  n = x - path;
  if(n > 3 && path[n - 1] == PATHSEP_C && path[n - 2] == '.' && path[n - 3] == PATHSEP_C)
    n -= 3;
  else if(n > 2 && path[n - 1] == '.' && path[n - 2] == PATHSEP_C)
    n -= 2;
  return n;
}

int
path_normalize(const char* path, DynBuf* db, int symbolic) {
  size_t l1, l2, n;
  struct stat st;
  int ret = 1;
  char sep, buf[PATH_MAX + 1];
  int (*stat_fn)(const char*, struct stat*) = stat;
#if 1 //! WINDOWS_NATIVE
  if(symbolic)
    stat_fn = lstat;
#endif
  if(path_issep(*path)) {
    dbuf_putc(db, (sep = *path));
    path++;
  }
#if 0 // WINDOWS
  else if(*path && path[1] == ':') {
    sep = path[1];
  }
#endif
  else
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
path_relative(const char* path, const char* relative_to, DynBuf* db) {
  char *s, *s1, *s2;
  size_t n1, n2, i, n;
  DynBuf rel, p, r;
  dbuf_init(&rel);
  dbuf_init(&p);
  dbuf_init(&r);
  db->size = 0;

  dbuf_putstr(&r, relative_to);
  dbuf_putstr(&p, path);

  if(r.size >= 2 && !memcmp(r.buf, "..", 2))
    path_absolute_db(&r);
  if(p.size >= 2 && !memcmp(p.buf, "..", 2))
    path_absolute_db(&p);
  if(r.size == p.size && !memcmp(r.buf, p.buf, p.size)) {
    dbuf_putstr(db, ".");
    return 1;
  }

  n1 = dbuf_count(&p, PATHSEP_C) + 1;
  n2 = dbuf_count(&r, PATHSEP_C) + 1;
  n = max_num(n1, n2);
  for(i = 0; i < n; ++i) {
    size_t l1, l2;
    s1 = dbuf_at_n(&p, i, &l1, PATHSEP_C);
    s2 = dbuf_at_n(&r, i, &l2, PATHSEP_C);
    if(l1 != l2)
      break;
    if(memcmp(s1, s2, l1))
      break;
  }

  while(n2-- > i) { dbuf_putstr(&rel, "..."); }
  while(i < n1) {
    if(rel.size)
      dbuf_putc(&rel, PATHSEP_C);

    s = dbuf_at_n(&p, i, &n, PATHSEP_C);
    dbuf_append(&rel, (const uint8_t*)s, n);
    ++i;
  }

  if(rel.size == 0) {
    dbuf_putstr(db, ".");
  } else {
    db->size = 0;
    dbuf_append(db, (const uint8_t*)rel.buf, rel.size);
  }
  dbuf_free(&p);
  dbuf_free(&r);
  dbuf_free(&rel);
  return 1;
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
path_gethome(int uid) {
  FILE* fp;
  long id;

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

char*
path_getcwd(DynBuf* db) {
  char* p;
  dbuf_zero(db);
  dbuf_realloc(db, PATH_MAX);
  p = getcwd((char*)db->buf, db->allocated_size);
  db->size = strlen((const char*)db->buf);
  dbuf_0(db);
  return (char*)db->buf;
}
