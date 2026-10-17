# Plan: `path_*` C functions onto the shish names (2026-10-17)

Status: R1-R12 done. `src/path.c` is deleted; `src/path-sys.c` holds the non-lexical
functions; the JS-only helpers are `static js_path_*` in `quickjs-path.c`.

Goal: `quickjs-path.c` and every other C client call `path_*` functions whose names do
not end in a digit, and the names common with `~/Sources/shish/lib/path.h` mean the same
thing in both projects. `src/path.c` goes away. The exact task list is in
[TODO.md](../TODO.md), Tier 22 (steps R1-R12; the Node-compat steps T, C, W, G of
[api-interface-path.md](api-interface-path.md) are in the same list, ordered so a rename and a
behaviour change never share a commit).

## 1. Where things stood (before R1; the work is done, see the status line above)

- `src/path-lib.c` (398 lines) already implements part of shish's `path.h`: `path_rootlen`,
  `path_basename`, `path_dirlen`, `path_cat`, `path_canon`, `path_abs`, `path_common`,
  `path_rel`, `path_extoff`, `path_listlen`, `path_listnext`, `path_search`.
- `src/path.c` (650 lines) holds the old digit-suffixed API (`path_exists1`, `path_at4`, ...),
  `include/path.h` declares both generations plus 14 `static inline` helpers and macros.
- The digit is the argument count, an old naming style (`path_length1(p)`, `path_length2(p, n)`).
  shish's rule is `(pointer, length)` always, caller's buffer out, no digit.
- No sibling `qjs-*` project and no `.js` file uses any of the digit-suffixed functions;
  all clients are inside this repo (section 3).

## 2. Target layout

| file | holds | rule |
| --- | --- | --- |
| `src/path-lib.c` | the implementation of shish's `path.h`, nothing else | no allocation, no syscalls except `path_gethome(uid)`'s file read; stralloc-free |
| `src/path-sys.c` (new) | non-lexical code: stat/lstat/getcwd/readlink/resolve/home lookup | `DynBuf` where shish says `stralloc` |
| `quickjs-path.c` | the JS-only helpers (`at`, `components`, `slice`, `toArray`, ...) as `static js_path_*` functions (digit-free names: `js_path_length`/`_z`, `js_path_at`/`_z`, `js_path_slice_into`, `js_path_isin`, `js_path_equal`, ...) | nothing here is exported to C |
| `include/path.h` | declarations of the two files above, the `path_is*` macros that stay | one header, same names as shish |
| `src/path.c` | deleted | |

`src/path-lib.c` after the migration adds the shish functions it lacks and has no stralloc:
`path_isabs`, `path_isrel`, `path_isname`, `path_next`, `path_parentlen`, `path_fnmatch`,
`path_gethome(int uid)` (this one lives in `src/path-sys.c`: it reads a file).

shish functions that take a `stralloc` (`path_cat_sa`, `path_abs_sa`, `path_rel_sa`) are not
ported: `path_cat`, `path_abs`, `path_rel` with a caller's buffer cover them (they return the
length needed when the buffer is too small, which is the two-call pattern shish's `_sa` wrap).
`path_getcwd`, `path_readlink` and `path_resolve` are ported to `src/path-sys.c` with
`DynBuf*` in place of `stralloc*` and otherwise the same prototype and return values; the
header says so in one line.

## 3. Mapping, old to new

### 3.1 Same function in shish: rename, same semantics unless noted

| old (digit) | new | clients | note |
| --- | --- | --- | --- |
| `path_fnmatch5(pat, plen, s, slen, flags)` | `path_fnmatch` | `quickjs-path.c` (fnmatch) | shish's 400-line version replaces `src/path.c`'s; diff the two on a battery first |
| `path_isabsolute2(p, n)` | `path_isabs(p, n)` | `quickjs-path.c` ×2 | Windows: `C:x` is relative now (was absolute) |
| `path_isabsolute1(p)` | `path_isabs(p, n)`, or `path_isabs_s(p)` | `include/path.h` macro `path_isrelative` | |
| `path_getcwd1(DynBuf*)` | `path_getcwd(DynBuf*)` | `quickjs-path.c` ×2, `path.c` | returns 1 / 0 as shish; separators normalised to `PATHSEP_C` |
| `path_getcwd0()` (malloc'd) | `path_getcwd(DynBuf*)` | `src/qjsm.c:1003` | caller frees with `dbuf_free` |
| `path_gethome1(uid)` | `path_gethome(int uid)` | `quickjs-path.c` (gethome) | |
| `path_gethome()` ($HOME) | `getenv("HOME")` at the call site, else `path_gethome(getuid())` | `src/compat/glob.c:361` | the name `path_gethome` changes meaning to shish's: uid lookup in `/etc/passwd` |
| `path_readlink2(p, DynBuf*)` | `path_readlink(p, DynBuf*)` | `quickjs-misc.c:970` | |
| `path_resolve3(p, DynBuf*, symbolic)` | `path_resolve(p, n, DynBuf*, symbolic, cwd, cwdn)` | `path.c` internal | |
| `path_realpath3(p, n, DynBuf*)` | `path_resolve(p, n, db, 0, NULL, 0)` | `quickjs-path.c` (realpath) | old one fails if the file does not exist, shish's keeps missing components: keep the `exists` check in `quickjs-path.c` |
| `path_isabs(p)`, `path_isname(p)` macros | `path_isabs(p, n)`, `path_isname(p, n)` functions | `src/qjsm.c` ×8 (`:1592,:1602,:1783,:1825,:1917,:1962,:2141,:2274`), `src/child-process.c:266` | the macros and the functions share a name with different arity: step R1 first renames the macros to `path_isabs_s(p)` / `path_isname_s(p)` / `path_isrel_s(p)` (shish's own `_s` suffix for NUL-terminated strings, cf. `path_basename_s`) at all 13 call sites, step R2 adds the functions and defines the `_s` macros through them |
| `path_isrelative(p)` macro | `path_isrel(p, n)`, `path_isrel_s(p)` | `src/qjsm.c:1785`, `quickjs-path.c` ×3 | |

### 3.2 Not in shish, non-lexical: new name, `src/path-sys.c`

| old | new | clients |
| --- | --- | --- |
| `path_exists1(p)`, `path_exists2(p, n)` | `path_exists(p, n)` | `src/qjsm.c:1785`, `quickjs-path.c` ×2, `path_search` callback |
| `path_isfile1(p)` | `path_isfile(p, n)` | `src/qjsm.c` ×3 (`:343,:975,:1393`) |
| `path_issymlink1(p)` | `path_issymlink(p, n)` | `quickjs-path.c` (Windows branch of `isSymlink`), `path_resolve` |
| `path_gethome2(user, n)` | `path_homeof(user, n)` | `src/compat/glob.c:378` |

`path_exists(p, n)` uses `lstat`; this stays, `src/qjsm.c` and the module search rely on it. The
JS `path.exists` disagreement with `fs.existsSync` is `BUGS`' `path-exists-true-for-dangling-symlink`
and is fixed in `quickjs-path.c` with its own `stat()` call, not by changing `path_exists`.

### 3.3 Not in shish, lexical: move into `quickjs-path.c` as `static`, or delete

These serve only the JS API (`at`, `components`, `length`, `slice`, `toArray`, `skip`,
`skipSeparator`, `right`, `isSeparator`, `isin`, `equal`, `getsep`). A rough search of
`lib/`, `examples/`, `utilities/` and the sibling projects found no user of `at`,
`components`, `skipSeparator`, `right`, `isSeparator`, `isin`, `equal`, `canonical`,
`search`; the project rule is "remove what nothing uses, if it has no standard target".

| old | becomes | clients |
| --- | --- | --- |
| `path_length1/2`, `path_components3`, `path_at3/4`, `path_slice4`, `path_diff4`, `path_equal4`, `path_isin4` | `static` in `quickjs-path.c`, rebuilt on `path_next`/`path_rootlen`; or deleted with their JS methods | `quickjs-path.c` only |
| `path_component1/3`, `path_separator1/2`, `path_skip1/2`, `path_right2`, `path_getsep1` | the same, or deleted | `quickjs-path.c` only |
| `path_skipdotslash1/2`, `path_compare2` | `static` in `src/qjsm.c` (one client each: `:531`, `:1775`); try `path_canon` for the second first | `src/qjsm.c` |
| `path_dup2/3` | deleted: `js_strndup`/`dbuf` at the two call sites | `src/path.c` only |

### 3.3.1 Usage audit of the qjsm-only methods (2026-10-17, step T5)

Searched: `lib/`, `examples/`, `utilities/` of this repo, `~/Sources/plot-cv` and every sibling
`~/Sources/quickjs/qjs-*`, `.js/.mjs/.cjs` files that import `path` (default, namespace or named import),
a hit being `ns.name` or the named import used. Locals called `path` (arrays, strings) were discarded by hand.

| method | users (file:line) | standard home | verdict |
| --- | --- | --- | --- |
| `canonical` `equal` `isin` `isSeparator` `getsep` `search` `right` `skipSeparator` `at` `components` | none | none | remove |
| `baselen` `basepos` `dirlen` `extlen` `extpos` | none | none | remove |
| `isCharDev` `isBlockDev` `isFIFO` `isSocket` | none | `fs.Stats` | remove |
| `readlink` | none | `fs.readlinkSync` | remove |
| `slice` `length` `toArray` | `plot-cv/upload-server.js:31,114,120,677,679` only (an own `isin`/dir splitter) | `split('/')` | remove after porting that script (author's call) |
| `absolute` | `lib/process.js:93`, `utilities/resolve-imports.js:142`, `utilities/list-exports.js:502,605`, plot-cv (8) | `path.resolve` | migrate callers to `resolve`, then remove |
| `isRelative` | `utilities/resolve-imports.js:2068`, `utilities/list-exports.js:509`, plot-cv (6) | `!isAbsolute` | migrate callers, then remove |
| `exists` `isFile` `isDirectory` | `utilities/resolve-imports.js` (15), `utilities/list-exports.js`, `utilities/source-scanner.js`, plot-cv, `qjs-opencv`, `qjs-debugger` | `fs.existsSync`, `fs.statSync(p).isFile()` | keep for now (many users); `exists` waits for decision C9 |
| `getcwd` | `lib/process.js:26,732` (it is `process.cwd`), `utilities/resolve-imports.js:144`, plot-cv | `process.cwd` | keep: `process.js` is built on it |
| `gethome` | `lib/repl.js:1428`, `qjs-debugger/gui/theme.js:15,16`, `plot-cv/google-contacts.js:42` | `os.homedir` | keep until qjsm has `os.homedir` |
| `realpath` | plot-cv (3) | `fs.realpathSync` | keep for now |
| `isSymlink` | `plot-cv/project-server.js:226` | `fs.lstatSync(p).isSymbolicLink()` | keep for now |
| `fnmatch` | `plot-cv/project-server.js:55,85,211`, `plot-cv/upload-server.js:574` | `path.matchesGlob` (phase G) | keep; revisit after G2 |

Nothing is deleted before the user answers (step X1).

Decision gate before any of 3.3 is rewritten: run the usage audit (task 1 of Tier 22), then
for each JS method either port it or delete method, test and doc row together.

The `_s` convention (shish: `path_basename_s(s)` is `path_basename(s, str_len(s), 0)`) also covers the
NUL-terminated callers of the new `src/path-sys.c` functions: `path_exists_s`, `path_isfile_s`,
`path_issymlink_s` are macros over `path_exists(p, n)` and the like.

## 4. Order of work

The authoritative, numbered list is TODO.md Tier 22 (R1-R12 here, plus T/C/W/G of
api-interface-path.md). What follows is the reasoning behind its order.

1. Audit and tests first: usage audit of the JS methods; a differential test for `path_fnmatch`
   (old against shish's) and for `path_realpath3` against `path_resolve`.
2. Port the missing shish functions into `src/path-lib.c` and declare them.
3. Create `src/path-sys.c` with the 3.1/3.2 functions; each old function becomes a one-line
   wrapper in the same commit only if a client still needs it, otherwise it is removed at once.
4. Migrate clients, one file per commit: `src/compat/glob.c`, `quickjs-misc.c`, `src/child-process.c`,
   `src/qjsm.c`, then `quickjs-path.c`.
5. Move or delete the 3.3 helpers.
6. Delete `src/path.c`, the digit-suffixed declarations and the `static inline` helpers from
   `include/path.h`; check the CMake source list (the sources are listed by glob, verify the
   deleted file does not leave a stale entry).

Verification after every step: `tests/unittests/test-path.js` (42 tests), `test-glob*` and the
qjsm tests under `ctest`; at the end `utilities/api-surface.sh` on the `path` module before and
after (`diff` must be empty apart from the deleted JS methods), and `rg 'path_[a-z]*[0-9]\('`
returns nothing.

## 5. Risks (all handled by R1-R12; checked 2026-10-17)

- Arity clash: `path_isabs`/`path_isname` exist today as 1-argument macros; shish's are
  2-argument functions. A half-done migration compiles into calls that silently use the macro.
  Step R1 renames the macros (`_s`) before step R2 introduces the functions, so the two never coexist
  under one name.
- `PATH_FNM_NOMATCH` and `PATH_FNM_PATHNAME` are both `1` in `include/path.h` and in shish's
  header (`fnmatch(3)` has `FNM_NOMATCH` as a return value, not a flag); keep, but never use
  `PATH_FNM_NOMATCH` as a flag.
- `path_gethome` changes meaning (`$HOME`, no argument, to `/etc/passwd`, by uid); the only
  client is `src/compat/glob.c`, which must keep `~` working from `$HOME`.
- Windows: `path_isabs` follows shish (`C:x` relative), and `path_issymlink` keeps the
  `is_symlink` branch; none of it can be run here, so the Windows branches are reviewed, not
  tested.
- `path_realpath3` fails on a missing file, `path_resolve` does not; the JS `realpath` keeps its
  current behaviour only if the existence check stays in `quickjs-path.c`.
