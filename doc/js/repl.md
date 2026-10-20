# repl

Source: `lib/repl.js` (pure JS)

An interactive read-eval-print loop with line editing, history, completion and
syntax highlighting.

## Piped mode

When stdout is not a tty the REPL runs in line mode, so a script or an LLM can
drive it over a pipe (`REPL.piped` is true):

- one command per input line, read until EOF, then the REPL finishes
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
