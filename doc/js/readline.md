# readline

Source: `lib/readline.js` — compiled into `qjsm`; also `readline/promises` (`lib/readlinePromises.js`)

Node's `readline` (also Bun's and Deno's) for line-by-line input, without Node streams.
`tests/unittests/test-readline.js` runs on qjsm, node, bun and deno; `test-readline-file.js`
covers the FILE input (`process.stdin`).

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
| `createInterface(input, output?)` / `createInterface({ input, output, prompt, signal })` | A new `Interface`. `prompt` defaults to `'> '`; an aborted `signal` closes it. |
| `Interface` | `EventEmitter`; events `'line'`, `'pause'`, `'resume'`, `'close'` |
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
| `terminal`, `line`, `history`, `input`, `output`, `closed` | `terminal` is always `false`; `closed` is `undefined` until closed, as in Node |

Lines end at `\n`, `\r\n` or `\r` (a `\n` at the start of the next chunk after a chunk-final
`\r` belongs to it); a last line without a terminator is emitted at the end of the input.
Lines of one chunk are all delivered, even if a handler closes the interface in the middle,
as in Node.

## Not implemented

`terminal` mode: raw-mode line editing, history, `completer`, `emitKeypressEvents`,
`Readline` of `readline/promises`. On a tty the kernel's own line editing applies.
