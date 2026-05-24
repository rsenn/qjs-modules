# events

Source: `lib/events.js` (pure JS) — default export: `EventEmitter`

Node-style `EventEmitter` plus a WHATWG-style `EventTarget`.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `EventEmitter` | class | `on`/`once`/`off`/`emit` event emitter. **(default export)** |
| `EventTarget` | class | DOM-style `addEventListener`/`removeEventListener`/`dispatchEvent`. |
| `eventify(self)` | function | Mixes emitter behavior into an existing object. |
