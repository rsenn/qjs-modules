# repl

Source: `lib/repl.js` (pure JS)

An interactive read-eval-print loop with line editing, history, completion and
syntax highlighting.

## Editing

The edit buffer holds the whole input, several lines included. Enter inserts
a newline, indented by the open brackets, while the input is incomplete: an
open bracket, template literal or block comment. Otherwise it submits.

| Key | Action |
| --- | --- |
| Enter | newline while the input is incomplete, else submit |
| Up / Down | the line above or below, on the first or last line the history |
| Home, ^A / End, ^E | start / end of the current line |
| ^K | kill to the end of the line, at its end the newline |
| Alt-Backspace | with a bracket mismatch, kill from it to the end (^Y brings it back), else the word before the cursor |
| ^C | drop the input, on an empty line print how to exit, again there exits |
| ^D | on an empty line exit, in editor mode submit |
| ^L | clear the screen |
| ^R / ^S | search the history backward / forward |

Brackets while typing:

- the innermost opener still open around the cursor is underlined
- right after a closer, the closer and its opener are shown inverse
- from the first mismatched closer (`a)` or `(]`) to the end, the input is
  shown on a red background, and Enter submits it instead of waiting for
  the missing bracket

Pasted text arrives as one piece (bracketed paste), so a multi-line paste is
edited as a whole and runs on Enter.

### Node.js behavior

- an error prints as `Uncaught TypeError: message`, followed by the stack
  frames below the REPL input, a thrown value as `Uncaught 7`
- `_` is the last result and `_error` the last error; assigning either stops
  the update, with `Expression assignment to _ now disabled.`
- `\editor` enters editor mode: Enter inserts newlines, ^D submits, ^C cancels
- `\break` drops the buffered lines of an unfinished piped input

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

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `REPL` | — | class | The REPL engine: prompt, line editing, history, completion, evaluation, output. |
| `REPLServer` | — | class | Wraps a `REPL` to serve a session (Node `repl`-style). |
| `loadModule(moduleName)` | 1 | async function | Dynamically imports a module for use inside the REPL. A leading `*` assigns its exports onto `globalThis`, `!` calls its default export, `=` binds the default export as `globalThis.<name>` (`\i =require`); with none the namespace is bound as `globalThis.<name>`. |
