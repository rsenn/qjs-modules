# abort

Source: `lib/abort.js` (pure JS) — default export: `AbortController`

WHATWG `AbortController` / `AbortSignal` implementation (built on `EventTarget`).

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `AbortSignal` | class | Signal object with `aborted`, `reason`, `onabort`, and an `abort` event; `extends EventTarget`. |
| `AbortController` | class | Holds a `signal` and an `abort(reason)` method that triggers it. **(default export)** |
