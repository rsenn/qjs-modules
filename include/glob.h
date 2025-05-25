/**   * Contains the declarations for the glob() API.
 */

#ifndef GLOB_H
#define GLOB_H

/** This API provides facilities for enumerating the file-system contents

*/

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

/*
 * Constants and definitions
 */

/* Flags */
#define GLOB_APPEND 0x0001  /* Append to output from previous call. */
#define GLOB_DOOFFS 0x0002  /* Use gl_offs. */
#define GLOB_ERR 0x0004     /* Return on error. */
#define GLOB_MARK 0x0008    /* Append / to matching directories. */
#define GLOB_NOCHECK 0x0010 /* Return pattern itself if nothing matches. */
#define GLOB_NOSORT 0x0020  /* Don't sort. */

#define GLOB_ALTDIRFUNC 0x0040 /* Use alternately specified directory funcs. */
#define GLOB_BRACE 0x0080      /* Expand braces ala csh. */
#define GLOB_MAGCHAR 0x0100    /* Pattern had globbing characters. */
#define GLOB_NOMAGIC 0x0200    /* GLOB_NOCHECK without magic chars (csh). */
#define GLOB_QUOTE 0x0400      /* Quote special chars with \. */
#define GLOB_TILDE 0x0800      /* Expand tilde names from the passwd file. */
#define GLOB_NOESCAPE 0x1000   /* Disable backslash escaping. */

/* Error values returned by glob(3) */
#define GLOB_NOSPACE (-1) /* Malloc call failed. */
#define GLOB_ABORTED (-2) /* Unignored error. */
#define GLOB_NOMATCH (-3) /* No match and GLOB_NOCHECK not set. */
#define GLOB_NOSYS (-4)   /* Function not supported. */
#define GLOB_ABEND GLOB_ABORTED

/*
 * Typedefs
 */

/** Result structure for glob()
 *
 * This structure is used by glob() to return the results of the search.
 */
typedef struct {
  int gl_pathc;    /**< count of total paths so far */
  int gl_matchc;   /**< count of paths matching pattern */
  int gl_offs;     /**< reserved at beginning of gl_pathv */
  int gl_flags;    /**< returned flags */
  char** gl_pathv; /**< list of paths matching pattern */

  int (*gl_errfunc)(const char*, int);

  /*
   * Alternate filesystem access methods for glob; replacement
   * versions of closedir(3), readdir(3), opendir(3), stat(2)
   * and lstat(2).
   */
  void (*gl_closedir)(void*);
  struct dirent* (*gl_readdir)(void*);
  void* (*gl_opendir)(const char*);
  int (*gl_lstat)(const char*, struct stat*);
  int (*gl_stat)(const char*, struct stat*);
} glob_t;

/*
 * API functions
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Generates pathnames matching a pattern
 *
 * This function is a pathname generator that implements the rules for
 * file name pattern matching used by the UNIX shell.
 *
 * @param pattern The pattern controlling the search
 * @param flags A combination of the <b>GLOB_*</b> flags
 * @param errfunc A function that is called each time part of the search
 * processing fails
 * @param pglob Pointer to a glob_t structure to receive the search results
 * @return 0 on success, otherwise one of the <b>GLOB_*</b> error codes
 */
int glob(char const* pattern, int flags, int (*errfunc)(char const*, int), glob_t* pglob);

/** Frees the results of a call to glob
 *
 * This function releases any memory allocated in a call to glob. It must
 * always be called for a successful call to glob.
 *
 * @param pglob Pointer to a glob_t structure to receive the search results
 */
void globfree(glob_t* pglob);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#include <list.h>
#include "getdents.h"
#include "buffer-utils.h"

struct vec {
  char** ptr;
  uint32_t len, res;
};

struct glob_state {
  int flags;
  PointerRange pat, buf;
  struct vec paths;
};

int my_glob(const char* pattern, struct glob_state* g);

#endif /* GLOB_H */
