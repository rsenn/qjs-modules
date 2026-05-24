# extendAsyncFunction

Source: `lib/extendAsyncFunction.js` (pure JS) — default export: `extendAsyncFunction`

Resolves the (otherwise unnamed) `AsyncFunction` constructor/prototype and
provides an installer hook for extending it.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendAsyncFunction(proto=AsyncFunctionPrototype)` | 0–1 | function | Installs extensions on the async-function prototype. **(default export)** |
| `AsyncFunctionExtensions` | — | const | The (currently empty) extension bag. |
| `AsyncFunctionPrototype`, `AsyncFunction` | — | const | The async-function prototype and its constructor. |
