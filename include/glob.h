/**   * Contains the declarations for the glob() API.
 */

#ifndef GLOB_H
#define GLOB_H

/** This API provides facilities for enumerating the file-system contents

*/

/*
 * Constants and definitions
 */

/* Error codes */
#define GLOB_NOSPACE \
  (1) /**< (Error result code:) An attempt to allocate memory failed, or \
 \ \
         if errno was 0 GLOB_LIMIT was specified in the flags and ARG_MAX \
         patterns were matched. */
#define GLOB_ABORTED \
  (2) /**< (Error result code:) The scan was stopped because an error was \
         encountered and either GLOB_ERR was set or \
         (*errfunc)() returned non-zero. */
#define GLOB_NOMATCH \
  (3)                           /**< (Error result code:) The pattern does not match any existing \
                                   pathname, and GLOB_NOCHECK was not set int flags. */
#define GLOB_NOSYS (4)          /**< (Error result code:) . */
#define GLOB_ABEND GLOB_ABORTED /**< (Error result code:) . */

/* Flags */
#define GLOB_ERR 0x00000001    /**< Return on read errors. */
#define GLOB_MARK 0x00000002   /**< Append a slash to each name. */
#define GLOB_NOSORT 0x00000004 /**< Don't sort the names. */
#define GLOB_DOOFFS \
  0x00000008 /**< Insert PGLOB->gl_offs NULLs. Supported from version 1.6 \
                of UNIXEm. */
#define GLOB_NOCHECK \
  0x00000010 /**< If nothing matches, return the pattern. Supported from \
                version 1.6 of UNIXEm. */
#define GLOB_APPEND \
  0x00000020 /**< Append to results of a previous call. Not currently \
                supported in this implementation. */
#define GLOB_NOESCAPE \
  0x00000040 /**< Backslashes don't quote metacharacters. Has no effect \
                in \
                this implementation, since escaping is not supported. */

#define GLOB_PERIOD \
  0x00000080 /**< Leading `.' can be matched by metachars. Supported from \
              * version 1.6 of UNIXEm. \
              */
#define GLOB_MAGCHAR \
  0x00000100 /**< Set in gl_flags if any metachars seen. Supported from \
                version 1.6 of UNIXEm. */
#define GLOB_ALTDIRFUNC \
  0x00000200 /**< Use gl_opendir et al functions. \
                  Not currently supported in this \
                  implementation. */
#define GLOB_BRACE \
  0x00000400 /**< Expand "{a,b}" to "a" "b". Not currently supported in \
                this implementation. */
#define GLOB_NOMAGIC \
  0x00000800 /**< If no magic chars, return the pattern. Supported from \
                version 1.6 of UNIXEm. */
#define GLOB_TILDE \
  0x00001000 /**< Expand ~user and ~ to home directories. Partially \
                supported from version 1.6 of UNIXEm: leading ~ is \
                expanded to %HOMEDRIVE%%HOMEPATH%. */
#define GLOB_ONLYDIR \
  0x00002000 /**< Match only directories. This implementation guarantees \
                to only return directories when this flag is specified. \
              */
#define GLOB_TILDE_CHECK \
  0x00004000 /**< Like GLOB_TILDE but return an GLOB_NOMATCH even if \
                GLOB_NOCHECK specified. \
                Supported from version 1.6 of UNIXEm. */
#define GLOB_ONLYFILE \
  0x00008000 /**< Match only files. Supported from version 1.6 of UNIXEm. \
              */
#define GLOB_NODOTSDIRS \
  0x00010000 /**< Elide "." and ".." directories from wildcard searches. \
                Supported from version 1.6 of UNIXEm. */
#define GLOB_LIMIT \
  0x00020000 /**< Limits the search to the number specified by the caller \
                in gl_matchc. Supported from version 1.6 of UNIXEm. */

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

#endif /* GLOB_H */
