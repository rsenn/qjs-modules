# console

Source: `lib/console.js` (pure JS) — default export: `ConsoleSetup`

A configurable `console` implementation built on the [`inspect`](inspect.md)
pretty printer.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Console(...args)` | * | function | Constructs a console object (`log`, `info`, `warn`, `error`, `debug`, …). |
| `ConsoleOptions(obj)` | 1 | function | Builds/normalizes a console options object (formatting, depth, colors). |
| `ConsoleSetup(inspectOptions, callback)` | 2 | function | Installs a global `console`; returns it. **(default export)** |
| `inspect` | — | re-export | Re-exported from the `inspect` module. |
