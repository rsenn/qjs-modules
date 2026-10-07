# events

Source: `lib/events.js` (pure JS) — default export: `EventEmitter`

Node-style `EventEmitter` plus a WHATWG-style `EventTarget`.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `EventEmitter` | class | Node's event emitter, see below. **(default export)** |
| `EventTarget` | class | DOM-style `addEventListener`/`removeEventListener`/`dispatchEvent`. |

## Node conformance

`EventEmitter` follows Node: handlers receive only the `emit()` arguments; `on`, `once`,
`off`, `addListener`, `prependListener`, `prependOnceListener`, `removeListener` and
`removeAllListeners` return the emitter; `emit()` returns whether a handler ran;
`emit('error')` without a listener throws; `newListener`/`removeListener` events;
`listeners()`, `rawListeners()`, `listenerCount()`, `eventNames()`, `setMaxListeners()`,
`getMaxListeners()`, `EventEmitter.defaultMaxListeners`. Event types may be strings or symbols.

| Export | Meaning |
| --- | --- |
| `once(emitter, type)` | promise of the `emit()` argument array (EventEmitter) or the event (EventTarget); rejects on `'error'` |
| `on(emitter, type, { signal }?)` | async iterator over `emit()` argument arrays |
| `getEventListeners(emitter, type)`, `listenerCount(emitter, type)` | |
| `getMaxListeners(emitter)`, `setMaxListeners(n, ...targets)`, `defaultMaxListeners` | |
