# perf_hooks

Source: `lib/perf_hooks.js` — compiled into `qjsm`; default export: `performance`

Node's `perf_hooks` (also Bun's and Deno's): High Resolution Time and User Timing, without
Node-only extras. `tests/unittests/test-perf-hooks.js` runs on qjsm, node, bun and deno.

```js
import { performance, PerformanceObserver } from 'perf_hooks';

performance.mark('start');
// ... work
performance.measure('work', 'start');                 // from mark 'start' to now
performance.getEntriesByName('work')[0].duration;     // milliseconds

new PerformanceObserver(list => console.log(list.getEntries())).observe({ entryTypes: ['measure'] });
```

## Exports

| Export | Description |
| --- | --- |
| `performance` | the `Performance` instance **(default export)** |
| `now()` | `performance.now()`, kept for older callers |
| `Performance`, `PerformanceEntry`, `PerformanceMark`, `PerformanceMeasure` | the classes |
| `PerformanceObserver`, `PerformanceObserverEntryList` | observing new entries |

## performance

| Member | Description |
| --- | --- |
| `now()` | milliseconds since the module loaded, fractional |
| `timeOrigin` | epoch milliseconds of that origin; `timeOrigin + now()` is the current time |
| `mark(name, { startTime, detail })` | records and returns a `PerformanceMark`; `startTime` defaults to `now()`; `TypeError` for a missing name or a negative or non-numeric `startTime` |
| `measure(name, startMark?, endMark?)` | a `PerformanceMeasure` between two marks (a name is the latest mark of it; `SyntaxError` if unset); `start` defaults to 0, `end` to `now()` |
| `measure(name, { start, end, duration, detail })` | `start`/`end` are mark names or timestamps; `end` from `start + duration`, `start` from `end - duration`; `TypeError` for all three, or with `endMark` |
| `getEntries()`, `getEntriesByName(name, type?)`, `getEntriesByType(type)` | recorded marks and measures, sorted by `startTime` |
| `clearMarks(name?)`, `clearMeasures(name?)` | forget those entries (all without a name) |
| `toJSON()` | `{ timeOrigin }` |

Entries have `name`, `entryType` (`'mark'` or `'measure'`), `startTime`, `duration`, `detail` (a
`structuredClone` of the option, default `null`) and `toJSON()`. `new PerformanceMark(name,
options)` builds one without recording it.

## PerformanceObserver

| Member | Description |
| --- | --- |
| `new PerformanceObserver(callback)` | `callback(list, observer)`; `TypeError` without a function |
| `observe({ entryTypes })` | observe those types (`'mark'`, `'measure'`), replacing the previous ones |
| `observe({ type, buffered })` | add one type; `buffered: true` also delivers the entries recorded before |
| `disconnect()` | stop observing and drop queued entries |
| `takeRecords()` | the queued entries, emptying the queue (no callback for them) |
| `PerformanceObserver.supportedEntryTypes` | `['mark', 'measure']` |

Entries of one turn arrive together in one callback, from a timer task after the current
code and its promise jobs. `list` is a `PerformanceObserverEntryList` with `getEntries()`,
`getEntriesByName()` and `getEntriesByType()`.

## Not implemented

`timerify`, `createHistogram`, `monitorEventLoopDelay`, `eventLoopUtilization`, `nodeTiming`,
resource timing and the Node-only entry types (`function`, `gc`, `http`, `net`, `dns`).
`measure()` with a number as a positional `startMark` treats it as a mark name (as in
Deno and Bun), not as a timestamp as Node does.
