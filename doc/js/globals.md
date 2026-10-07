# globals

Source: `lib/globals.js` — qjsm only; opt-in

Installs the web and Node globals QuickJS lacks. A global that already exists is never
replaced. Importing the module is the whole API.

```js
import 'globals';

new URL('https://example.com/a?x=1').searchParams.get('x'); // '1'
structuredClone({ d: new Date(0) });
```

| Global | Source |
| --- | --- |
| `URL`, `URLSearchParams` | `url` |
| `TextEncoder`, `TextDecoder` | `textcode` |
| `AbortController`, `AbortSignal` | `abort` |
| `EventTarget` | `events` |
| `Blob` | `blob` |
| `ReadableStream`, `WritableStream`, `TransformStream`, `*QueuingStrategy`, `TextEncoderStream`, `TextDecoderStream` | `streams` (the full WHATWG classes of `lib/stream.js`, with `pipeTo()`/`pipeThrough()`); `Blob.prototype.stream()` returns the same class |
| `setTimeout`, `setInterval`, `setImmediate` and their `clear*` | `timers` |
| `performance`, `Performance`, `PerformanceEntry`, `PerformanceMark`, `PerformanceMeasure`, `PerformanceObserver`, `PerformanceObserverEntryList` | `perf_hooks` |
| `atob`, `btoa` | here (Latin-1 strings, padding optional, `InvalidCharacterError`) |
| `queueMicrotask` | here (a promise job) |
| `structuredClone` | here: Date, RegExp, boxed primitives, ArrayBuffer, typed arrays, Map, Set, Error, cycles; `transfer` option (ArrayBuffers); SharedArrayBuffer returned as is; `DataCloneError` for functions, symbols, Promise, WeakMap, WeakSet, WeakRef; `TypeError` without arguments; the error is a plain `Error`, not a `DOMException` |
| `crypto` | here: `getRandomValues()` and `randomUUID()` from `/dev/urandom`; no `subtle` |

Not installed: `Buffer`, `Worker`, `fetch` (see `qjs-lws`).

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `structuredClone(value, options?)` | function | The same function that is installed as a global. |
