# Plan: the `path` interface against Node, Bun and Deno (2026-10-17)

Goal: `import path from 'path'` and `import path from 'node:path'` give, under qjsm, what
`node:path` gives in Node, Bun and Deno, so a script written once runs unchanged. The C
renames in [api-implement-path.md](api-implement-path.md) and the findings in
[api-compare-path.md](api-compare-path.md) are part of the same work; the step list that
orders all three is in [TODO.md](../TODO.md), Tier 22.

## 1. Findings

Dumps made with `./dump-node-path.sh` (re-generates `/tmp/node-path-surface/`: for node, bun,
deno and qjsm, `*.args.txt`, `*.noargs.txt`, one sorted name file per group, `compare.txt`).
Versions: node 22.23.3, bun 1.4.2, deno 2.9.7.

| | node | bun | deno | qjsm `path` |
| --- | --- | --- | --- | --- |
| named functions | 13 | 13 (same) | 14 (+ `common`) | 52 names, 13 shared |
| named values | `sep` `delimiter` | same | same | + `FNM_*` |
| objects | `default` `posix` `win32` | same | same | `default` `posix`, no `win32` |
| `posix`, `win32` members | the 17 names each | same | same | `posix` = the module, no `win32` |
| `sep`, `delimiter` | `/` `:` (posix), `\` `;` (win32) | same | same | posix only |
| parameter names (`--flat`) | `basename(path, suffix)` ... | none (native) | minified (`e`, `t`) | none |

Behaviour (234-case battery, see api-compare-path.md): node, bun and deno print identical output;
qjsm differs only in argument validation and the missing `win32`.

Quirks that are **not** targets:

- Bun: every function is `bound X` with `length` 1 (node: `basename.length` 2, `join` 0,
  `relative` 2, `resolve` 0); `keys` order differs from node.
- Deno: the module namespace has an extra named export `common` (std's `common(paths)`), not on
  the default export.
- `path.matchesGlob` is present in all three (experimental in node's docs).
- Error text: `TypeError` and `code: 'ERR_INVALID_ARG_TYPE'` are the same in all three, the message is not.
  Node: `The "path" argument must be of type string. Received type number (5)`. Bun: `The "path" property
  must be of type string, got number` (and `join('a', null)` names `paths[1]`). Deno copies Node's wording
  but calls `basename`'s second parameter `ext` and reports `relative('a', 5)` as `"path"`.

## 2. What "compatible" is, per name

| name | now | target |
| --- | --- | --- |
| `basename` `dirname` `extname` `isAbsolute` `parse` `join` | done (C1-C3): `js_path_check_string` in `quickjs-path.c`; was: coerce non-strings | `TypeError`, `code: 'ERR_INVALID_ARG_TYPE'`, Node's message word for word (the part all three share is name and code) |
| `normalize` `relative` `resolve` `format` | done (C4): Node's `code` and message; `relative(x)` stays qjsm's one-argument form | check `code` and message equal Node's |
| function `length` | done (C5); was 1 for all | node's: `basename` 2, `join` 0, `relative` 2, `resolve` 0, others 1 |
| `toNamespacedPath`, `_makeLong` | missing | identity on posix; the same function object |
| `win32` | missing | full Windows implementation, `posix`/`win32` cross links |
| `matchesGlob` | missing | node 22 semantics |
| `path/posix`, `path/win32`, `node:path/...` | do not resolve | resolve |
| the 38 qjsm-only names | exported | stay for now (Tier 22 decides each); never affect the 17 shared names |

## 3. Architecture

Options for `win32`, the one big piece:

| | idea | verdict |
| --- | --- | --- |
| A | all in C: the lexical core (`src/path-lib.c`) switches to Windows rules at run time | no: the core is built for one OS by macro (`WINDOWS`), turning it into a run-time switch touches every function |
| B | a JS layer `node/path` that only `node:path` maps to (nodeHooks layer mechanism, like `node_events`) | no: bare `path` is what portable scripts import (quickjs-scripting skill), it would stay un-Node |
| **C** | native `path` stays the module for both specifiers and becomes Node-correct for the shared names; `win32` is a pure-JS builtin `pathWin32` that `path.win32` loads lazily | yes |

C in files:

| file | role |
| --- | --- |
| `quickjs-path.c` | validation, `length`s, `toNamespacedPath`/`_makeLong`, lazy `win32` getter (`globalThis.requireModule('pathWin32')`, as `lib/fs.js` does for `fs.promises`) |
| `lib/pathWin32.js` (new builtin) | Node's win32 algorithms in JS, the same 17 names, `sep` `\`, `delimiter` `;`; imports nothing but `process` (lazily) |
| `src/qjsm.c`, subpath table (next to `fs/promises`) | `path/posix` to `path`, `path/win32` to `pathWin32` |
| `lib/nodeHooks.js` | no code change: `node:path` and `node:path/*` already pass their name on; only its comment table gets the rows |
| `cmake/qjsm.cmake` | `pathWin32` in `BUILTINS_COMPILED`, `set(pathWin32_MODULES process)` |
| glob engine (section 6) | `path.matchesGlob` |

The win32 port follows Node's `lib/path.js` (MIT licence; credit in the file header). It is a
JS port of the algorithms, not a copy of the file: no primordials, ES module, no
`internalBinding`. `process.cwd()` and `process.env` are only read inside `resolve` and
`toNamespacedPath`.

## 4. Per runtime

| runtime | what to do |
| --- | --- |
| Node | the reference. `node:path` is the oracle for every golden table. |
| Bun | nothing to add: Bun's `path` is Node's. `lib/bun.js` only imports `dirname` from `path`; it exports no `Bun.path`. Bun's `bound`/`length` quirks are not copied. Re-run the golden test under bun (ctest `.bun`) after every compat step. |
| Deno | `tests/deno-import-map.json` already maps `path` to `node:path`, so `@portable` tests run. The skill's `deno-import-map.json` maps only four names; adding `path` there and a `path` entry to `modules.yaml` is a change to the `quickjs-scripting` skill and needs the user's approval. Deno's std `jsr:@std/path` (`common`, `SEPARATOR`, `toFileUrl`, `globToRegExp`, ...) is a different API: an optional `lib/stdPath.js` in the style of `lib/jsonStreams.js` (a port of a Deno std module), only if something needs it; `path_common` from `src/path-lib.c` already does `common`'s job. |

## 5. Tests

Status: T2-T4 are written and pass (`utilities/gen-path-golden.mjs`, `test-path-node.js` under qjsm, node,
bun and deno, `test-path-interface.js` under qjsm); `test-path-win32.js` waits for phase W.

- Oracle: node 22 is run once by `utilities/gen-path-golden.mjs`, which prints the golden
  tables (inputs and node's outputs) as JS source. The expected values are literals in the test, so
  bun, deno and qjsm are compared against node without node being present.
- `tests/unittests/test-path-node.js`, marked `@portable`: posix, valid inputs; ctest runs it under
  qjsm, node, bun, deno.
- `tests/unittests/test-path-interface.js`, qjsm only: `Function.length`, error `code` and Node's exact message,
  the exported name list. A case that fails today is added in the commit that fixes it.
- `tests/unittests/test-path-win32.js`, `@portable`: the win32 golden tables, added when
  `path.win32` is wired in.
- After each phase `./dump-node-path.sh` is re-run; its `compare.txt` shows the remaining gap.

## 6. `matchesGlob`

`fnmatch(3)` has no `**` and no `{a,b}`, so it cannot do Node's semantics. The same engine is needed
by `Bun.Glob`, `fs.glob`/`globSync` and the glob argument of `Bun.Archive` (TODO Tier 19). The plan builds
one engine (pattern to matcher: `*`, `**`, `?`, `[...]`, `{a,b}`, leading `!`), golden-tested against
node's `path.matchesGlob`, and `path.matchesGlob` is its first user. If Tier 19 gets there first, reuse
its engine. This is the last step, the other steps do not depend on it.

## 7. Order and coordination with the C migration

Three streams touch `quickjs-path.c`; they are ordered so that no commit mixes a rename with a
behaviour change.

| stream | steps (TODO.md Tier 22) | rule |
| --- | --- | --- |
| T tests and tools | T1-T4 | first; every later step is checked against them |
| R renames (api-implement-path.md) | R1-R12 | behaviour-neutral; one function family per commit |
| C compat (this plan, posix) | C1-C9 | adds code at the entry of `js_path_method` or new functions; never edits a `path_*` call |
| W win32 | W1-W12 | new files plus one getter; independent of R |
| G glob | G1-G2 | last |

Dependencies: R1-R3 before C1 (the isabs/isrel/isname functions are what the validation helper's
neighbours call); C steps and R5-R12 may be done in either order; W needs T4 and C7; G needs nothing
but is placed last. A step that changes a result a doc table quotes updates that table in the same
commit (`doc/api-compare-path.md`, `doc/native/path.md`).

## 8. Risks and open questions

- Error messages: Node's wording is the target (`Received an instance of Object`, `Received type number (5)`,
  `Received null`); Bun and Deno differ from it and from each other (section 1), so the `@portable`
  test asserts only `TypeError` and the code, and the exact text is checked in the qjsm-only test T3.
- `path.exists` (lstat) and the type tests are qjsm extras; whether `exists` should follow symlinks
  is step C9, a decision, not a port.
- `win32` `resolve` reads per-drive current directories from `process.env` (`=C:`); on POSIX this is
  tested with injected values only, never against the real environment.
- `_makeLong === toNamespacedPath` must hold as object identity; two exports bound to one function.
- Keys order of the exports object differs between node, bun and deno; nothing to copy.
- Deno: the skill change in section 4 waits for approval.
