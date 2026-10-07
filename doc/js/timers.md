# timers

Source: `lib/timers.js` (pure JS)

Node-style interval timers, layered on `os.setTimeout`. Importing the module also
installs `setTimeout`, `clearTimeout`, `setInterval` and `clearInterval` on
`globalThis`, without replacing any that already exist.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `setTimeout` | — | re-export | Re-exported from `os`. |
| `clearTimeout` | — | re-export | Re-exported from `os`. |
| `setInterval(fn, t)` | 2 | function | Repeatedly invokes `fn` every `t` ms; returns an id. |
| `clearInterval(id)` | 1 | function | Cancels an interval started by `setInterval`. |
| `setImmediate(fn, ...args)` | 1+ | function | Runs `fn` after the current tick (a zero-delay timer); returns a handle. |
| `clearImmediate(handle)` | 1 | function | Cancels a pending `setImmediate`. |

`setImmediate` and `clearImmediate` are installed on `globalThis` as well. The promise forms are in [timersPromises](timersPromises.md) (`timers/promises`).
