# repl

Source: `lib/repl.js` (pure JS). It imports `terminal/core` (tty I/O, widths, escape writers) and `terminal/keys` (`readKey`), see [terminal](terminal.md#modules).

An interactive read-eval-print loop with line editing, history, completion and
syntax highlighting.

## Editing

The edit buffer holds the whole input, several lines included. Enter inserts
a newline, indented by the open brackets, while the input is incomplete: an
open bracket, template literal or block comment. Otherwise it submits.

| Key | Action |
| --- | --- |
| Enter | newline while the input is incomplete, else submit |
| Alt-Enter | a newline, indented as Enter would, never submits |
| Up / Down | the line above or below, on the first or last line the history |
| Home, ^A / End, ^E | start / end of the current line |
| Right, ^E at the end | insert the grey suggestion or completion, if there is one |
| Tab | complete, a second Tab lists the candidates |
| ^K | kill to the end of the line, at its end the newline |
| Alt-Backspace | with a bracket mismatch, kill from it to the end (^Y brings it back), else the word before the cursor |
| ^C | drop the input, on an empty line print how to exit, again there exits |
| ^D | on an empty line exit, in editor mode submit |
| ^L | clear the screen |
| ^R / ^S | search the history backward / forward |

Pasted text arrives as one piece (bracketed paste), so a multi-line paste is
edited as a whole and runs on Enter.

Column widths are counted in terminal cells: a wide character (`漢`) takes two,
so the cursor and the wrapping stay right.

### While typing

| What | When | Shown |
| --- | --- | --- |
| Brackets | always | the innermost opener still open around the cursor is underlined, right after a closer the closer and its opener are inverse, from the first mismatched closer (`a)` or `(]`) to the end the input is on a red background and Enter submits it instead of waiting for the missing bracket |
| Suggestion | the buffer is the start of a single-line history entry | the rest of the newest such entry, grey, after the cursor |
| Completion preview | exactly one completion is left (`Obj`) and no suggestion | the rest of it (`ect`), grey, after the cursor |
| Result preview | a single line holding a literal (`0x10`) or a plain property chain (`Math.PI`) | the value, grey, on the row below |
| Syntax check | the input is complete but does not parse (`1 +`) | the prompt is red |
| Dedent | a `)`, `]` or `}` typed at the start of an indented line | steps back one level (two spaces) |

- the suggestion and the previews show only with the cursor at the end of the
  buffer; none is drawn on the submitted line
- the result preview evaluates nothing: it reads values by property descriptor,
  a getter ends the chain without a preview, and an expression such as `1 + 1`
  has none. A value equal to what was typed (`5`, `true`) is not repeated
- an unfinished input (an open bracket) is not a syntax error, nor is a
  directive
- the list a second Tab prints shows the kind of each entry: `max(2)` a
  function and its argument count, `now()` a function without arguments,
  `JSON.` an object, a plain name any other value. Getters are not called

A buffer taller than the terminal is drawn as the lines around the cursor that fit, the rest
is not on screen until the cursor moves to it (Up and Down scroll the window). One logical
line that is taller than the terminal by itself is still drawn whole.

In a ^R search the painted line may wrap over several rows. Enter moves below all of them
before it runs the match, and any other key that leaves the search clears every row of it.

A method of the `REPL` can be replaced after construction, `repl.controlC = function() { ... }`:
the key table is resolved by name on each key, so the replacement is the one that runs.

### Auto-close and vi mode

Both are off by default.

`\autoclose` turns auto-closing on and off:

- typing `(`, `[`, `{` inserts the closer too, when the next character is
  nothing, a space, a closer, a comma, a semicolon or a dot: `f(|)`
- typing a quote inserts a pair unless a word character is next to it, so
  `it's` stays; typing the closer or quote that is already next is typed over
- Backspace between an empty pair (`(|)`) removes both
- pasted text is never changed

`\vi` turns vi key bindings on and off. The cursor shape shows the mode (bar
in insert mode, block in normal mode) and every new prompt starts in insert
mode. Esc, or Esc followed by a command key, enters normal mode.

| Normal mode | Does |
| --- | --- |
| `h` `l` | left, right |
| `0` `^` `$` | start of the line, first non-blank, last character |
| `w` `b` | next word start, previous word start |
| `x` `X` | delete the character under / before the cursor |
| `D` `C` `S` | delete to the end of the line, the same and insert, replace the line |
| `i` `I` `a` `A` | insert at the cursor / line start, after the cursor / at the line end |
| `k` `j` | previous, next history entry or line |
| `p` | paste what was last deleted or yanked, after the cursor |
| `dw` `db` `d$` `d0` `dd` | delete by motion, `dd` the line |
| `cw` `cb` `c$` `c0` `cc` | the same, then insert |

Control keys, the arrows and Enter keep their usual meaning in both modes.
There is no undo, no count and no `e`, `r`, `u`.

### Directives

A line that starts with a backslash is a directive. `\h` lists them all.

| Directive | Does |
| --- | --- |
| `\type expr` | the same as `expr?`, see Introspection |
| `\time expr` | evaluates `expr`, prints the result and `time: N ms`, an `await` included |
| `\autoclose` | toggles auto-closing, prints `autoclose on` or `off` |
| `\vi` | toggles vi mode, prints `vi mode on` or `off` |
| `\editor` | editor mode: Enter inserts newlines, ^D submits, ^C cancels |
| `\break` | drops the buffered lines of an unfinished input |
| `\t` | toggles the timing display in the prompt |

### Introspection

`expr?` or `\type expr` prints what is known about the value:

```
> [1, 2]?
type    object
class   Array
proto   Array.prototype -> Object.prototype
length  2
keys    3
```

A function adds its name, argument count and source (the first 20 lines).
A trailing `?` is never valid JavaScript, so it cannot clash with a ternary.
`describeValue(value)` returns the lines and is exported.

### Results and helpers

- `_` is the last result and `_error` the last error; assigning either stops
  the update, with `Expression assignment to _ now disabled.`
- every result except `undefined` is also kept as `$1`, `$2`, ..., the last
  100 of them. On a terminal the number is printed before the result (`$3 = 7`)
- these helpers are defined on `globalThis` unless a global of that name exists:

| Helper | Does |
| --- | --- |
| `copy(x)` | puts a string, else its JSON, in the yank buffer (^Y) and, on a terminal, in the system clipboard through OSC 52 |
| `keys(o)`, `values(o)` | `Object.keys`, `Object.values` |
| `table(data, columns?)` | prints a box table, a row per entry (`formatTable()` returns the text) |

```
> table([{ a: 1 }, { a: 2, b: 'x' }])
┌─────────┬───┬─────┐
│ (index) │ a │ b   │
├─────────┼───┼─────┤
│ 0       │ 1 │     │
│ 1       │ 2 │ 'x' │
└─────────┴───┴─────┘
```

### Node.js behavior

- an error prints as `Uncaught TypeError: message`, followed by the stack
  frames below the REPL input, a thrown value as `Uncaught 7`
- the first ^C drops the input, a second one on an empty line exits

### Options

Properties of the `REPL` object, set before `run()` or at any time:

| Property | Default | Meaning |
| --- | --- | --- |
| `suggest` | `true` | grey history suggestion and completion preview |
| `preview` | `true` | grey result preview below the input |
| `syntaxCheck` | `true` | red prompt while the input cannot parse |
| `autoClose` | `false` | auto-close brackets and quotes (`\autoclose`) |
| `viMode` | `false` | vi key bindings (`\vi`) |
| `bracketedPaste` | `true` | ask the terminal for bracketed paste |

The suggestion, the previews and the red prompt need colors: with
`showColors` off or output piped they are not drawn.

## Piped mode

When stdout is not a tty the REPL runs in line mode, so a script or an LLM can
drive it over a pipe (`REPL.piped` is true):

- one command per input line, read until EOF, then the REPL finishes; an
  input with an open bracket continues on the next line
- no escape sequences, no raw tty mode, no syntax highlighting, and
  `inspect()` output has `colors: false`
- the prompt is printed once per ready state (`> `), so a controller reads
  the result up to the next prompt
- an `await` command holds back the queued lines until it settles

```sh
printf '1+1\nawait fetch(url)\n' | qjsm repl-script.js
```

## Relation to LineEditor

`REPL` edits its input itself: its own key table (`commands`, keyed by raw sequences), its own redraw (`render()`, `paintLine()`) and its own history search. `LineEditor` in [terminal](terminal.md#lineeditor) is the same kind of editor as a state machine over decoded keys, without drawing. The two are separate for now. What `LineEditor` lacks before `REPL` can be ported onto it is listed in `TODO.md`, "Port lib/repl.js onto terminal.js LineEditor": the missing keys (transpose, case of a word, quoted insert, Ctrl-L, forward search), a renderer, syntax coloring with bracket matching, a completion list, vi mode and a key table by key name.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `REPL` | — | class | The REPL engine: prompt, line editing, history, completion, evaluation, output. |
| `REPLServer` | — | class | Wraps a `REPL` to serve a session (Node `repl`-style). |
| `isValidSyntax(src)` | 1 | function | Whether `src` compiles as a function body or an async function body. Runs nothing. |
| `describeValue(value)` | 1 | function | The lines `expr?` prints: type, class, prototype chain, size, and for a function its name, argument count and source. |
| `formatTable(data, columns?)` | 2 | function | The box table `table()` prints, as a string. |
| `loadModule(moduleName)` | 1 | async function | Dynamically imports a module for use inside the REPL. A leading `*` assigns its exports onto `globalThis`, `!` calls its default export, `=` binds the default export as `globalThis.<name>` (`\i =require`); with none the namespace is bound as `globalThis.<name>`. |

## Export list

Generated by `utilities/doc-api.js` from the `repl` dump of `utilities/api-surface.sh`: 139 members. Edit the text above this block, not the block.

**Functions:** `describeValue()`, `formatTable()`, `isValidSyntax()`, `loadModule()`

### `REPL`

class, constructible

- **methods:** `abort()`, `acceptGhost()`, `acceptLine()`, `addCleanupHandler()`, `alert()`, `backwardChar()`, `backwardDeleteChar()`, `backwardKillLine()`, `backwardKillMismatch()`, `backwardKillWord()`, `backwardWord()`, `beginningOfLine()`, `bigfloatToString()`, `bigintToString()`, `bracketStyles()`, `cmdReadlineStart()`, `cmdStart()`, `colorizeJs()`, `completion()`, `completionLabel()`, `completionPreview()`, `continuation()`, `controlC()`, `controlD()`, `controlL()`, `defineHelpers()`, `defineLastValues()`, `deleteAll()`, `deleteChar()`, `deleteCharDir()`, `downcaseWord()`, `dupstr()`, `endOfLine()`, `escape()`, `evalAndPrintStart()`, `exit()`, `extractDirective()`, `forwardChar()`, `forwardSearch()`, `forwardWord()`, `getCompletions()`, `getContextObject()`, `getContextWord()`, `getDirective()`, `ghost()`, `handleByte()`, `handleChar()`, `handleCmd()`, `handleCmdEnd()`, `handleDirective()`, `handleKey()`, `handleMouse()`, `hasDirective()`, `help()`, `historyAdd()`, `historyClear()`, `historyFile()`, `historyLoad()`, `historyNext()`, `historyPrevious()`, `historySave()`, `historySearch()`, `historySearchBackward()`, `historySearchForward()`, `insert()`, `insertNewline()`, `insertTyped()`, `introspect()`, `killLine()`, `killRegion()`, `killWord()`, `lineEnd()`, `lineMove()`, `lineStart()`, `loadSaveOptions()`, `makeFilename()`, `modulesLoad()`, `modulesSave()`, `mouseTracking()`, `moveBelow()`, `numberToString()`, `paintLine()`, `pasteChar()`, `pasteStart()`, `pipeChar()`, `pipeDrain()`, `previewText()`, `print()`, `printEvalError()`, `printEvalResult()`, `printFunction()`, `printPromise()`, `printResult()`, `printStatus()`, `printTimeOnce()`, `quotedInsert()`, `readlineHandleCmd()`, `readlinePrintPrompt()`, `readlineRemovePrompt()`, `readlineStart()`, `render()`, `reset()`, `reverseSearch()`, `run()`, `saveOptions()`, `searchCb()`, `setViMode()`, `setupMouseTracking()`, `show()`, `showCompletions()`, `sigintHandler()`, `skipWordBackward()`, `skipWordForward()`, `suggestion()`, `termInit()`, `termReadHandler()`, `transposeChars()`, `transposeWords()`, `upcaseWord()`, `update()`, `viKey()`, `viShape()`, `viWordStart()`, `write()`, `yank()`
- **properties:** `directives`, `[Symbol.toStringTag]`

### `REPLServer`

class, constructible

- **methods:** `clearBufferedCommand()`, `defineCommand()`, `displayPrompt()`, `setupHistory()`

