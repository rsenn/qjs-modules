# timers

Source: `lib/timers.js` (pure JS)

Node-style interval timers, layered on `os.setTimeout`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `setTimeout` | — | re-export | Re-exported from `os`. |
| `clearTimeout` | — | re-export | Re-exported from `os`. |
| `setInterval(fn, t)` | 2 | function | Repeatedly invokes `fn` every `t` ms; returns an id. |
| `clearInterval(id)` | 1 | function | Cancels an interval started by `setInterval`. |
