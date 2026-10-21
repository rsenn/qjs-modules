# bun

Source: `lib/bun.js` (pure JS) — named exports: `plugin`, `file`, `write`, `stdin`, `stdout`, `stderr`; the default export is installed as `globalThis.Bun` by qjsm, with `Intl`, and `Bun.serve`/`Bun.fetch` and the network globals when qjs-lws is installed ([below](#globals-from-other-modules))

Bun's runtime plugin API (https://bun.com/docs/runtime/plugins), built on
`registerHooks()` (`doc/qjsm.md`). Only the runtime half exists here: there is no bundler,
so `onStart`/`onEnd`/`onBeforeParse` are accepted and ignored.

```js
Bun.plugin({
  name: 'demo',
  setup(build) {
    build.module('virtual', () => ({ exports: { default: 42 } }));
    build.onResolve({ filter: /^yaml:/ }, ({ path }) => ({ path: path.slice(5), namespace: 'yaml' }));
    build.onLoad({ filter: /.*/, namespace: 'yaml' }, ({ path }) => ({ contents: `export default ${JSON.stringify(path)}`, loader: 'js' }));
  },
});

import v from 'virtual';        // 42
Bun.plugin.clearAll();
```

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `plugin({ name?, setup(build) })` | function | Runs `setup(build)`; returns its result (a Promise if `setup` is async). |
| `plugin.clearAll()` | function | Removes every registered plugin. |

## `build`

| Member | Description |
| --- | --- |
| `onResolve({ filter, namespace? }, cb)` | `cb({ path, importer, namespace, kind })` returns `{ path, namespace? }` or `undefined` to fall through. `namespace` defaults to `'file'` and is matched against the importer's. |
| `onLoad({ filter, namespace? }, cb)` | `cb({ path, namespace, loader })` returns `{ contents?, loader?, exports? }` or `undefined`. |
| `module(specifier, cb)` | Virtual module; `cb()` returns what `onLoad` would. |

## Differences from Bun

| Bun | Here |
| --- | --- |
| Callbacks may be async | Synchronous only — a Promise result throws `TypeError` |
| Loaders `ts`, `tsx`, `jsx`, `toml`, `yaml`, `css`, ... | Only `js`, `json` and `text`; others throw `TypeError` (no transpiler) |
| `typeof Bun` is the Bun runtime | `Bun` carries only what is listed here (roadmap: `TODO.md` Tier 15); do not use it to detect Bun |
| Plugins in `bunfig.toml` `preload` | Call `Bun.plugin()` before the `import()` it should affect (static imports are resolved first) |

## Bun.file / Bun.write

```js
const f = Bun.file('data.json');         // path, file: URL or fd; { type } overrides the MIME type
f.size; f.type; f.name; f.lastModified;  // sync stat; 0 / 2**52-1 when missing
await f.text(); await f.json(); await f.bytes(); await f.arrayBuffer();
for await (const chunk of f.stream()) {}
await f.slice(-10).text();               // a window, nothing is read before it is asked
await Bun.write('out/a.txt', 'hi');      // creates parent directories; resolves to bytes written
const w = Bun.file('log.txt').writer();  // FileSink: truncates; write() buffers, flush()/end() write
```

`Bun.file()` returns a `Blob` subclass that reads lazily. Reading a missing file rejects with
`ENOENT: no such file or directory, open 'x'` (`code`, `errno`, `syscall`, `path` set); a
directory with `EISDIR`; `exists()` is false for directories. `Bun.write` takes a string,
ArrayBuffer/view, `Blob`/`BunFile`, or anything with `arrayBuffer()`; numbers and objects are
stringified like Bun does, `null`/`undefined` throw `TypeError`. `Bun.stdin/stdout/stderr` are
BunFiles over fd 0/1/2 (`stdin.size` is `Infinity`).

| Bun | Here |
| --- | --- |
| `Bun.file(...)` is a plain `Blob` (`constructor.name`) | a `BunFile` subclass of `Blob`; `instanceof Blob` holds |
| `slice(0, 100000).size` is `100000` | clamped to the file size |
| `FileSink.flush()` may return a Promise | returns the number synchronously (also fine to `await`) |

## Globals from other modules

`lib/bun.js` provides the globals Bun has built in: `Intl` from `lib/intl.js`, and the network globals when
qjs-lws (the sibling project `../qjs-lws/`) is installed in the module path:

| Global | From | Enumerable |
| --- | --- | --- |
| `fetch`, `Bun.fetch` | `fetch` | yes |
| `Request`, `Response` | `serve` | yes |
| `Bun.serve` | `serve` | yes |
| `Headers` | `lws/headers` | yes |
| `FormData` | `lws/formdata` | yes |
| `URL`, `URLSearchParams` | `lws/url` | no, as in Bun |
| `AbortController`, `AbortSignal` | `lws/abort` | yes |
| `Intl` | `intl` (built in, [English only](intl.md)) | no |

- Nothing is loaded at startup: qjsm only checks that the module is a builtin or found by
  `locateModule()`, and the first read of the global loads it with `requireModule()` (qjs-lws costs ~50 ms).
- A global that already exists is left alone. Assigning one (`globalThis.URL = X`) replaces it.
- Without qjs-lws, or when the module fails to load, the global is absent: `typeof fetch` is
  `'undefined'`.
- `Bun.serve` and `Bun.fetch` are on the `Bun` object only, not named exports of `bun`.
