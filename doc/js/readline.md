# readline

Source: `lib/readline.js` — compiled into `qjsm`; also `readline/promises` (`lib/readlinePromises.js`)

Node's `readline` (also Bun's and Deno's) for line-by-line input, without Node streams.
`test-readline.js` (line mode) and `test-readline-terminal.js` (keystrokes; the expected
bytes are recorded from node) run on qjsm, node, bun and deno; `test-readline-file.js` covers
the FILE input (`process.stdin`).

```js
import * as readline from 'readline';

const rl = readline.createInterface({ input: process.stdin, output: process.stdout });

for await (const line of rl) console.log(line);        // every line of stdin
rl.question('name? ', name => console.log(name));      // prompt, take one line
```

## `input`

| Input | How it is read |
| --- | --- |
| `process.stdin`, any FILE (`fileno()`) | `os.setReadHandler()` on the fd; UTF-8 decoded, never cut inside a character |
| WHATWG `ReadableStream`, async iterable | pulled chunk by chunk; strings or bytes (`Uint8Array`, `ArrayBuffer`) |
| emitter with `'data'` / `'end'` | what node, bun and deno pass (a `Readable`); `pause()`/`resume()` forwarded |

A missing or unusable `input` throws `TypeError`.

## Exports

| Export | Description |
| --- | --- |
| `createInterface(input, output?, completer?, terminal?)` / `createInterface({ input, output, prompt, signal, terminal, completer, history, historySize, removeHistoryDuplicates, tabSize, crlfDelay, escapeCodeTimeout })` | A new `Interface`. `prompt` defaults to `'> '`; an aborted `signal` closes it. |
| `Interface` | `EventEmitter`; events `'line'`, `'pause'`, `'resume'`, `'close'` |
| `emitKeypressEvents(stream)` | make an emitter stream emit `'keypress'` `(char, key)` for its `'data'` |
| `promises` | `{ Interface, createInterface }` of `readline/promises` |
| `cursorTo(stream, x, y?, cb?)` | `ESC[<x+1>G`, or `ESC[<y+1>;<x+1>H` with `y` |
| `moveCursor(stream, dx, dy, cb?)` | `ESC[nD`/`C` then `ESC[nA`/`B` |
| `clearLine(stream, dir, cb?)` | `ESC[1K` (`dir < 0`), `ESC[0K` (`dir > 0`), `ESC[2K` |
| `clearScreenDown(stream, cb?)` | `ESC[0J` |

The helpers write to anything with `write()` or `puts()`; a `null` stream does nothing.

## Interface

| Member | Description |
| --- | --- |
| `question(query, [options], cb)` | writes `query` to `output`; the next line goes to `cb` instead of `'line'`. `options.signal` cancels it. Throws `ERR_USE_AFTER_CLOSE` once closed. A second `question()` while one is pending only prompts. |
| `prompt()`, `setPrompt(s)`, `getPrompt()` | write / change / read the prompt; `prompt()` resumes a paused interface |
| `write(data)` | feeds `data` to the line splitter as if it had been typed |
| `pause()`, `resume()` | stop / restart reading; emit `'pause'` / `'resume'` |
| `close()` | emits `'pause'` then `'close'`, once; the end of the input closes it too |
| `[Symbol.asyncIterator]()` | the lines; ends at `'close'` |
| `terminal`, `line`, `cursor`, `history`, `input`, `output`, `columns`, `closed` | `terminal` defaults to whether `output` is a tty; `closed` is `undefined` until closed, as in Node |
| `getCursorPos()` | `{ rows, cols }` of the cursor, counting the prompt, wide characters and wrapping at `columns` |
| `clearLine()` | moves to the end of the line, writes `\r\n` and empties the line |
| `'history'` event | `(history)` after every non-empty line |
| `'SIGINT'`, `'SIGTSTP'` events | ctrl-c / ctrl-z when there is a listener; ctrl-c without one closes the interface |

Lines end at `\n`, `\r\n` or `\r` (a `\n` at the start of the next chunk after a chunk-final
`\r` belongs to it); a last line without a terminator is emitted at the end of the input.
Lines of one chunk are all delivered, even if a handler closes the interface in the middle,
as in Node.

## Terminal mode

With `terminal: true` (the default when `output` is a tty) the input is read as keystrokes and
the line is edited and redrawn with the same escape sequences as Node. A tty FILE input
(`process.stdin`) is put into raw mode and restored on `close()`. Any other input works too,
which is how the tests drive it.

| Keys | Action |
| --- | --- |
| printable text, paste | inserted at the cursor; a line ending presses enter |
| `return`, `enter` | emit `'line'` (`\r\n` counts once within `crlfDelay`), add to `history` |
| left / right, ctrl-b / ctrl-f | one character (a surrogate pair is one) |
| home / end, ctrl-a / ctrl-e | start / end of the line |
| ctrl-left / right, meta-b / meta-f | previous / next word |
| backspace, ctrl-h / delete, ctrl-d | delete left / right; ctrl-d on an empty line closes |
| ctrl-u / ctrl-k | delete to the start / end of the line (kept for ctrl-y) |
| ctrl-w, ctrl-backspace, meta-backspace / meta-d, ctrl-delete | delete the word left / right |
| ctrl-y | insert the last deleted text |
| up / down | history, limited to entries starting with the text before the cursor |
| ctrl-p / ctrl-n | history, without the prefix filter |
| tab | with a `completer`: complete; without: insert a tab |
| ctrl-l | clear the screen and redraw |
| ctrl-c, ctrl-z | `'SIGINT'` / `'SIGTSTP'` if listened to; ctrl-c otherwise closes |

`completer(line)` returns `[completions, completeOn]`, or a promise of it; `completer(line,
callback)` calls `callback(err, [completions, completeOn])`. A common prefix is inserted; a
second tab lists the completions in columns. The interface is paused while it runs.
`historySize` defaults to 30, `history` seeds it (newest first), `removeHistoryDuplicates`
drops older equal entries.

A lone escape key is held `escapeCodeTimeout` ms (500) in case it starts a sequence.
Two tabs arriving in one chunk are not special-cased (Node inserts a literal tab then).

## Not implemented

Kill-ring rotation (meta-y), undo/redo, the `Readline` class of `readline/promises`, and
`output` resize events from a FILE (an emitter output's `'resize'` redraws).
