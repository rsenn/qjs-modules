# move-modules

Moves JS source files and rewrites every relative import of them (`utilities/move-modules.js`, `qjsm` only,
it lexes with `module-lexer`).

```sh
move-modules.js [options] SRC DST [SRC DST ...]
move-modules.js [options] -m MAPFILE
```

| Option | Meaning |
| --- | --- |
| `-m, --map=FILE` | one `old/path.js new/path.js` per line, `#` comments allowed; paths relative to the cwd |
| `-r, --root=DIR` | directory or file scanned for importers, repeatable (default: cwd) |
| `-x, --exclude=PATTERN` | skip names matching PATTERN (`*`, `?`), repeatable, added to the defaults `node_modules .git attic vendor lib` |
| `-X, --no-default-excludes` | drop the default list |
| `-n, --dry-run` | print the plan, change nothing |
| `-g, --git` | `git mv` the files, so history follows |
| `-s, --strict` | a relative specifier to a missing file is an error, not a warning |
| `-S, --strings=MODE` | `rewrite` (default) or `warn`: path mentions in string literals |
| `-R, --no-refs` | imports only: do not look at path mentions in comments, strings and text |

## What it does

1. Resolves all moves against the original tree, then rewrites the importers, then moves. Order in the map does
   not matter; two files moving together get their specifier recomputed from both new places.
2. For every `.js`/`.mjs` under the roots (and every moved file, even in an excluded dir) it rewrites the relative
   specifiers of `import ... from`, `export ... from`, `import 'x'`, `import('x')` with a string literal and
   `new URL('x', import.meta.url)`. Only the specifier text between the quotes changes, the rest of the file stays
   byte-identical; quotes, the written extension and the `./` / `../` style are kept. Bare specifiers are never touched.
3. Rewrites path mentions in comments, strings, template literals (static parts) and plain text, when one
   existing file of the original tree, in the move map, is provably meant (next section).
4. Prints `moved N files, rewrote M specifiers in K files` (`would ...` with `-n`).

Rerun with the same map: every destination exists and no source remains, so it prints `nothing to do` and exits 0.

## Path mentions

A candidate is a run of `[A-Za-z0-9_./@-]` with a `/` that ends in `.js .mjs .json .css .html .md`; a trailing
`.` and a `:line:col` suffix are left out of the rewrite. JS files are split with the ECMAScript lexer into
comments, strings and template parts (regex literals and `${}` substitutions are never touched); every other text
file is one region. It is read in up to three ways, each checked against the original tree:

| | Reading | Rewritten to | Used in |
| --- | --- | --- | --- |
| (a) | relative to the mentioning file's old dir | relative to its new dir, `./`/`../` as written | all |
| (b) | relative to the cwd | new path relative to the cwd | all |
| (c) | tail of exactly one moved path (`editor/model.js`) | longest tail of the new path that starts at the mention's first directory (`editor/view/model.js`), else the shortest unique tail (shown as a notice) | comments and text only |

Exactly one distinct moved target: rewritten. Two targets, or an existing unmoved file whose name is a moved one:
listed as ambiguous, nothing changed. Nothing existing: ignored. With (a) and (b) on one target, a comment keeps
the form it was written in (`./`, `../` means (a)), a string uses (b).

- A string is rewritten only for (b) (or an absolute path); a hit only by (a) is listed as "strings left alone",
  its meaning (file or cwd) cannot be told. `--strings=warn` lists all of them and rewrites none.
- URLs (`http://`, `//host/`) are ignored. A `/client/x.js` that is a moved file's tail is listed as url-like.
- A folder (`client/editor/`) or a glob (`client/editor/*.js`) that loses files is listed per file with line
  numbers, never rewritten.
- A mention touching a `${}` substitution is skipped.

## Conservative choices

- Refuses (exit 1, nothing changed) when a source is missing, a destination exists (also a destination that is
  another move's source: no chains or swaps), a source or destination is listed twice, or only part of the map is
  already applied.
- A relative specifier to a file that is missing in the original tree is a warning and stays as written; `-s` makes it an error.
- A specifier containing a backslash escape is reported and left alone.
- `new URL(...)` is found with a text pattern, not the lexer, so one in a comment or string is rewritten too.
- A directory import (`'./dir'`) is a missing file here: browser ES modules, no extension or index magic.
- Files are written as UTF-8 text through `FILE.new` and a rename that keeps the mode; only changed files are written.
