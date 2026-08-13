# repl

Source: `lib/repl.js` (pure JS)

An interactive read-eval-print loop with line editing, history, completion and
syntax highlighting.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `REPL` | — | class | The REPL engine: prompt, line editing, history, completion, evaluation, output. |
| `REPLServer` | — | class | Wraps a `REPL` to serve a session (Node `repl`-style). |
| `loadModule(moduleName)` | 1 | async function | Dynamically imports a module for use inside the REPL. |
