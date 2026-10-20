# console

Source: `lib/console.js` (pure JS) — default export: `ConsoleSetup`

A configurable `console` implementation built on the [`inspect`](../native/inspect.md)
pretty printer.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Console(...args)` | * | function | Constructs a console object (`log`, `info`, `warn`, `error`, `debug`, …). |
| `ConsoleOptions(obj)` | 1 | function | Builds/normalizes a console options object (formatting, depth, colors). |
| `ConsoleSetup(inspectOptions, callback)` | 2 | function | Installs a global `console`; returns it. **(default export)** |
| `inspect` | — | re-export | Re-exported from the `inspect` module. |

## Streams

The constructor takes Node's forms; `log`, `info` and `debug` write to the
first stream, `error` and `warn` to the second:

```js
new Console(stdout, stderr, { inspectOptions });          // error, warn to stderr
new Console({ stdout, stderr, inspectOptions });          // the same
new Console(stream, { inspectOptions });                  // everything to stream (Node does the same)
new Console({ inspectOptions });                          // std.out, and std.err for error and warn
```

qjsm's global `console` is `new Console(std.out, std.err, …)`, so
`console.error` goes to stderr as in node, bun and deno.
