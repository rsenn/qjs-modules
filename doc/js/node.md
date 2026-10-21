# node

Source: `lib/node.js` (pure JS, qjsm only) — no exports; qjsm imports it before the script runs, `-N`
(`--no-node`) turns it off. [bun](bun.md) imports it and adds the `Bun` global; `-B` loads `node` alone.

Makes qjsm resolve modules and see the globals the way Node does:

| Part | What | From |
| --- | --- | --- |
| loaders | `node:fs` and the other `node:` names | `lib/loaders/node.js`, see [qjsm](../qjsm.md#module-resolution-pipeline) |
| | bare names in `node_modules`, `package.json` `exports`/`imports`/`main` | [loaders/packages](loaders/packages.md) |
| | `.cjs` as CommonJS (sets `globalThis.require`) | [loaders/commonjs](loaders/commonjs.md) |
| globals, at startup | `URL`, `URLSearchParams`, `AbortController`, `AbortSignal`, `EventTarget`, `TextEncoder`, `TextDecoder`, `Blob`, the WHATWG streams, `atob`, `btoa`, `queueMicrotask`, `structuredClone`, `crypto`, `performance`, `Performance*`, `setImmediate`, `Intl` | [globals](globals.md) |
| | `global` (enumerable, as in Node) | here |
| globals, on first read | `fetch` (enumerable), `Request`, `Response`, `Headers`, `FormData`, `WebSocket` | qjs-lws, when it is in the module path |
| | `Buffer` | `node/buffer` |
| | `File` | `file` |
| | `Event`, `CustomEvent`, `DOMException` | `dom` |
| | `WebAssembly` | [webassembly](webassembly.md) |

```js
// no imports needed under qjsm
Buffer.from('hi').toString('hex');                   // '6869'
global === globalThis;                               // true
const res = await fetch('https://example.com/');     // with qjs-lws installed
```

- A global that already exists is never replaced, and assigning one replaces it.
- The globals read on first use cost nothing at startup: qjsm only checks that the module is a
  builtin or found by `locateModule()` (qjs-lws costs ~50 ms to load). Without the module the
  global is absent: `typeof fetch` is `'undefined'`.
- `process` is not part of it: qjsm installs that one itself (`-P` turns it off).

## Missing compared with Node

| Node global | Here |
| --- | --- |
| `MessageChannel`, `MessagePort`, `MessageEvent`, `BroadcastChannel` | absent |
| `CompressionStream`, `DecompressionStream` | absent |
| `navigator`, `localStorage`, `sessionStorage`, `Storage` | absent |
| `crypto.subtle`, `Crypto`, `CryptoKey`, `SubtleCrypto` | absent (`crypto` has `getRandomValues()` and `randomUUID()`) |
| `URLPattern`, `DisposableStack`, `AsyncDisposableStack`, `SuppressedError` | absent |
| `require` in an ES module | present, set by the CommonJS loader; Node has it only in CommonJS |
