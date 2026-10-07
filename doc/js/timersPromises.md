# timersPromises

Source: `lib/timersPromises.js` — import as `timers/promises` (also `node:timers/promises`); qjsm only

Node's `timers/promises`.

```js
import { setTimeout as sleep } from 'timers/promises';

await sleep(100, 'value'); // 'value' after 100 ms
```

| Export | Meaning |
| --- | --- |
| `setTimeout(delay, value?, { signal }?)` | resolves `value` after `delay` ms; an aborted `signal` rejects with `AbortError` |
| `setImmediate(value?, options?)` | resolves on a later tick |
| `setInterval(delay, value?, { signal }?)` | async iterator yielding `value` every `delay` ms |
| `scheduler.wait(delay, options?)`, `scheduler.yield()` | `setTimeout` / `setImmediate` without a value |
