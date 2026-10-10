# extract-c.js

Pulls functions, types, `#define`s and identifiers out of C/C++ sources
without compiling them: a lexer-based scanner, not a compiler. Output is
source text, a list, or JSON that `jq` can query. A REPL (`-r`) opens on the
gathered data, with a colored source dumper and a mouse-driven call-graph
browser.

```sh
extract-c.js -F function -l src/                   # every function, as file:line:col
extract-c.js -t include/ | jq '.[] | select(.size > 64)'
extract-c.js -i -c -d -o ids.json src/             # reference counts + unreached names
extract-c.js -r -i -d -E -I include src/           # explore it all in the REPL
```

Run it with `qjsm` (it imports `std`/`os`, so it is QuickJS-only). A `DIR`
argument stands for every `*.c`/`*.h` below it; `-` reads standard input.

## What it looks at

| Mode (flag) | Records | Found by |
| --- | --- | --- |
| default | function definitions, as source text | `findFunctions()`: a `)` followed by `{`, body ends at the next `}` in column 1 |
| `-t` | struct/union/class/enum/typedef/using, with field offsets and sizes (LP64) | `findTypes()` |
| `-F define,macro` | `#define CONST 1` (kind `define`) and `#define F(x) ...` (kind `macro`) | `findDefines()` |
| `-i` | every identifier: declaration, prototypes, references | `findIdentifiers()` |

`#define`s and conditionals are always read from the unpreprocessed text.
With `-E`/`-e` everything else is read from the preprocessed text.

## Options

| Option | Meaning |
| --- | --- |
| `-p REGEXP` | only names matching |
| `-l` | list lines instead of source / JSON (`-i -l`: `file:line:col: kind name<TAB>prototypes<TAB>references`) |
| `-t` / `-i` | types / identifiers, as JSON |
| `-f` | with `-i`, also struct/union fields (`<parent>.<field>`) |
| `-F KIND[,KIND]` | only these kinds: `function define macro typedef struct union class enum`; alone it emits all matching records as JSON (functions carry `.text`), with `-t`/`-i` it narrows them |
| `-c` | with `-i`, `references` is the count, not the list |
| `-C EXPR` | with `-i`, only counts matching `N`, `<N`, `<=N`, `>N`, `>=N`, `!=N` or the range `MIN,MAX` |
| `-d`, `-m NAME[,NAME]` | dependency walk from the entry points (default `main`); names no walk reaches go to stdout |
| `-E`, `-e PROG`, `-I DIR` | preprocess with `cpp`/`gcc -E`, or `PROG`; `-I` is passed on (only with `-E`/`-e`) |
| `-L MODE[,MODE]` | position shape in JSON: `line offset loc range file start end` |
| `-s DIR` | write each function to `DIR/<name>.c` |
| `-o FILE` | write to `FILE`; for JSON, `jq` examples are printed to stderr |
| `-r` | open the REPL before writing the output |

A progress counter `n/total` is redrawn on stderr when it is a terminal.

### References and dependencies

Two views of the same edges. A **reference** to `parse` made inside `main` is a **dependency** of
`main` on `parse`.

| | Means | JSON (`-i`) | REPL |
| --- | --- | --- | --- |
| `references` | who uses this name | list of positions (with `in`), or the count with `-c` | `ids.get(name).references` |
| `dependencies` | what this name's body uses | list of `{ name, file/line/column… }`, or the count with `-c` | `dependencies(name)` |

Whatever has a body owns the references in it: a function's body and signature, a struct/union/enum
(member types, array-size macros, enumerator values), a typedef, a variable's initializer
(`{ cb }` makes `tbl` depend on `cb`), a macro's body. Locals and parameters are not entered, and
member accesses (`pp.i.v`) are not recorded, so dependencies are on types, functions, variables and
macros, not on fields. References from prototypes stay unowned (roots for `-d`).

```sh
extract-c.js -i -c -F function src/ | jq '.[] | select(.dependencies == 0)'   # leaf functions
```

### Dependency walk (`-d`)

Edges run from the function or macro enclosing a reference to the name
referenced. The walk starts at the entry points (each gains one synthetic
reference, so `main` counts as used) and at *file-scope roots*: references
outside any function (initializers, tables) keep their targets alive. It can
miss dead code but does not report live code as dead. Matching is by bare
name, so same-named statics in different files are one node.

```sh
extract-c.js -i -c -F function -C 0 src/ | jq -r '.[].name'   # zero references
extract-c.js -d -l -F function src/                           # unreached from main
```

### Preprocessing (`-E`, `-e`, `-I`)

Included headers' content is dropped and lines stay on the line they came
from (via the `# N "file"` markers), so line numbers match the original.
Offsets, ranges and function text refer to the expanded source; macros are
expanded and `#define`s are gone, so with `-d` a macro used only through
expansion shows as unreferenced. Types declared in headers are not scanned,
so a struct holding a header typedef can have an unknown size.

A project header that shadows a system one (`-I src` with `src/features.h`)
breaks glibc; use `-e 'cpp -iquote src'` instead.

## The REPL (`-r`)

Opens after the files are read, before output is written (also when Ctrl-C
is pressed once while scanning; twice aborts). Needs a terminal on stdin.
Leave with Ctrl-D or `\q`; the output is then written from whatever the
globals hold.

| Global | Holds |
| --- | --- |
| `files` | the input files |
| `ids` | `Map` name -> `{ name, declaration, prototype[], references[] }`; every position has `file line column offset end endLine endColumn` and, inside a function, `in` |
| `records` | the JSON that gets written; reassign to change the output |
| `chunks` | the text output; reassign to change it |
| `dead` | with `-d`, identifiers no walk reaches |
| `findFunctions findTypes findDefines findIdentifiers` | the scanners |

| Directive | Does |
| --- | --- |
| `\help` | what is on `globalThis`, with live counts |
| `\banner` | the startup banner again |
| `\filter [file RE] [kind K,..] [name RE]` | limit `show()`; `\filter clear [key...]` resets, `\filter` shows it |
| `\browse [function]` | the call-graph browser |
| `\mouse [on\|off]` | clicks on `show()` output (on by default; `off` gives the terminal its selection and wheel back) |
| `\editor CMD {loc}` | editor template for right clicks: `{loc}` is `file:line:col`, also `{file} {line} {column}`; default `subl {loc}` or `sublime_text {loc}` if on PATH, else `$EXTRACT_C_EDITOR` |
| `\clip CMD` | clipboard command for left clicks (default `DISPLAY=:0 xclip -in`, also run for the `clipboard` selection; `$EXTRACT_C_CLIP`); without a working command the OSC 52 escape is used |

### `show(value)` / `format(value, opts)`

Records print with `show()` automatically; everything else uses `inspect()`.
It lays out identifiers (declaration, prototypes, references with
`in <function>`), types (size, align, field table), defines, macros and
functions, with the source found by file and line/column, C syntax
highlighting and the identifier token emphasised. xterm-256color. Options:
`color width lines refs items read`. Records not passing `\filter` are left
out (file: declaration's file, kind, name; references are not filtered).

### Clicking show() output

The output of the latest `show()` (until the next command runs) is clickable.

| Click | Does |
| --- | --- |
| left | works out what is under the cursor: the source `file:line:col` (on source text, the character clicked) and the JSON pointer of the part shown, e.g. `.references[3]`, `.declaration.file`, `.fields[1].type`; shows both and the code in a status row at the bottom of the screen; inserts `globalThis.records[2].references[3]` into the prompt (a second click replaces it); copies the absolute `file:line:col` to the clipboard |
| right | opens that location in the editor (`subl /abs/file.c:88:14` makes Sublime Text jump there) |

### The info pane

A left click on an identifier in source text (or on a record's name) also opens a small box at the
lower left of the screen, drawn with `lib/terminal.js` (`Screen.box()`, `setScrollRegion()`):

```
┌─────────── mid ───────────┐
│ function · static          │   kind, prototypes
│ /src/x.c:6:12 · lines 6–8  │   declaration, body range
│ referenced 1×  ·  1 dependency
│ click: show(ids.get("mid"))│
└────────────────────────────┘
```

The content above scrolls up and the rows leave the scroll region, so the pane never covers
output, and it is redrawn after every prompt update. Clicking the pane runs `show()` on that record
as if typed (what was already on the prompt line is kept). A name that is not declared in the
scanned files (a local, a field, `printf`) gets a pane saying so, and clicking it does nothing. The
pane closes when the next command runs, in the scrollback view it is drawn over the frame.

### Scrolling back through earlier output

Mouse reporting takes the wheel away from the terminal's own scrollback, so the REPL has its own.
The wheel (up) or PgUp at the prompt opens every earlier `show()` output (the last 100) on the
alternate screen, newest at the bottom; the old output is clickable too.

| Input | Does |
| --- | --- |
| wheel, PgUp / PgDn, ↑ ↓, Home / End | scroll |
| left / right click | as on the prompt: pointer + location in the status bar, code into the prompt, clipboard / editor |
| wheel down or PgDn at the end, `q`, Enter, Esc | back to the prompt |

PgUp keeps its usual history-search meaning while `\mouse` is off or nothing was shown yet.

The code starts from `records[i]`, `dead[i]`, `ids.get("name")` when the shown value is one
of those, else from `globalThis.shown`, which holds the value of the last `show()`.

How it works: mouse reporting (SGR 1006) is switched on, the bottom row is kept out of the
scroll region for the status bar, and every click first asks the terminal for the cursor row
(`ESC [ 6 n`); the prompt's start row follows from that, the output sits right above it, and
rows wrapped by the terminal are counted. Mouse and cursor reports are taken out of the input
before the line editor sees them. Reporting mouse events means the terminal's own selection
needs Shift and the wheel no longer scrolls the scrollback; `\mouse off` undoes that.

Limits: at the prompt only the latest `show()` output can be clicked (older ones in the scrollback view); a resize or a cleared screen
invalidates it; the terminal must answer the cursor query.

### Call graph

```js
dependencies('Outer')     // [{ name, file, line, column, ... }]: everything the body uses
callees('mid')            // the dependencies of mid that are defined functions
callers('leaf')           // [{ caller, file, line, column }]
reachable('main')         // Set of names; reachable('leaf', true) follows callers
callTree('main', { depth: 3, up: false })   // indented text, ↻ marks recursion
browse('main')            // full-screen browser
```

`callees` only counts references to functions *defined* in the scanned
files; `printf` and friends do not appear.

## The call-graph browser

`\browse [function]` / `browse(name)` shows one function body with its call
sites highlighted; clicking a call descends into the callee.

```
main › mid › leaf                              breadcrumb (clickable)
■ function leaf  src/x.c:1–3  0 calls          title
 1 │ static int leaf(int v) {                   body: line numbers,
 2 │   return v + 1;                            C highlighting, call
 3 │ }                                          sites underlined
called by: mid                                 callers
click/Enter open · … · q quit               keys / status
```

| Input | Action |
| --- | --- |
| left click on a call | descend into the callee |
| left click on a breadcrumb | jump back to that level |
| right click, Backspace, Esc, `h`, `u` | back one level (at the top: quit) |
| Tab, →, `n` / Shift-Tab, ←, `p` | next / previous call |
| Enter, Space, `l` | descend into the selected call |
| wheel, ↑ ↓ PgUp PgDn Home End | scroll |
| `q`, Ctrl-C, Ctrl-D | quit |

Every dependency in the body is highlighted: bright and underlined where it can be opened
(a function, macro, struct/union/enum or typedef defined in the scanned files), dim underlined
otherwise (`printf`, variables). Tab / ← → skip the dim ones; clicking one says why nothing
opens. A struct, typedef, enum or macro opens just like a function, showing its own body.

### Design

Three layers, so everything but the loop is testable without a terminal:

1. **Graph** (`callees`, `callers`, `reachable`, `callTree`): built from
   `ids`; every reference with an `in` field is an edge `in -> name`.
2. **Browser state** (`makeBrowser`, `renderBrowser`, `browserEvent`,
   `parseInput`): a stack of frames `{ name, file, from, to, lines, sites,
   top, sel }`. A function's extent comes from `findFunctions()` on the raw
   file (fallback: the declaration line plus 30 lines). `renderBrowser()`
   returns the screen rows plus the clickable regions (breadcrumb ranges and
   call-site rectangles, 1-based screen coordinates); `browserEvent()` maps
   an event onto the state using the last layout; `parseInput()` splits
   terminal bytes into key and SGR-mouse (`ESC [ < b ; x ; y M/m`) events.
3. **Loop** (`browse`): alternate screen, hidden cursor, mouse reporting
   (`?1049 ?25 ?1000 ?1006`); blocking `os.read` on stdin, redraw after each
   event, everything restored in a `finally`. The REPL is paused meanwhile,
   so no event loop is needed.

### Limits

- Function extent is heuristic (`}` in column 1); one-line functions and
  unusual brace styles can end up with the wrong range.
- Redraws happen on input only, so a terminal resize shows after the next key.
- Source files are cached per path for the session; edits are not picked up.
- Long lines are cut at the screen width; there is no horizontal scroll.
- With `-E`, call sites come from the preprocessed text but are shown from
  the raw file, so calls produced by macro expansion have no highlighted
  token.

## Tests

`tests/unittests/test-extract-c.js` covers the scanners, `format()`/`show()`
and its filter, the call-graph functions, `parseInput()` and the browser's
click/key handling (`qjsm tests/unittests/test-extract-c.js`).
