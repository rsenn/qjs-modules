# bun

Source: `lib/bun.js` (pure JS) — named exports: `plugin`, `file`, `write`, `stdin`, `stdout`, `stderr`, `mmap`; the default export is installed as `globalThis.Bun` by qjsm, on top of [node](node.md)'s globals, with the [utilities](#utilities) of `lib/bun/util.js` and `Bun.serve`/`Bun.fetch` when qjs-lws is installed ([below](#globals-from-other-modules))

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
| Loaders `ts`, `tsx`, `jsx`, `toml`, `css`, ... | Only `js`, `json` and `text` in a plugin; others throw `TypeError` (no transpiler). A `.yaml`/`.yml` import works without a plugin, see [YAML](#yaml) |
| `typeof Bun` is the Bun runtime | `Bun` carries only what is listed here (roadmap: `TODO.md` Tier 19); do not use it to detect Bun |
| Plugins in `bunfig.toml` `preload` | Call `Bun.plugin()` before the `import()` it should affect (static imports are resolved first) |

## `bun:<name>` imports

Loading `bun` registers a resolve hook: `bun:bun` is this module, `bun:<name>` is `bun/<name>` in the module path, so `bun:sqlite` is [bun-sqlite](bun-sqlite.md). A name with no such module throws as any missing module does.

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

## YAML

https://bun.com/docs/runtime/yaml. Source `lib/bun/yaml.js` (loaded on first use) over the native [yaml](../native/yaml.md)
module; `Bun.YAML` and `import { YAML } from 'bun'` exist when the yaml module is in the module path, and `parse` needs it
built with CYAML (`-DBUILD_LIBCYAML=ON` or an installed copy), else a call throws.

```js
import { YAML } from 'bun';                       // or Bun.YAML

YAML.parse('name: John\nhobbies:\n  - coding\n'); // { name: 'John', hobbies: ['coding'] }
YAML.parse('---\na: 1\n---\nb: 2\n');             // [{ a: 1 }, { b: 2 }]
YAML.stringify({ a: [1, 'x y'] });                // '{a: [1,x y]}'
YAML.stringify({ a: [1, 'x y'] }, null, 2);       // 'a: \n  - 1\n  - x y'

import config, { database } from './config.yaml'; // default and the top-level keys
```

| Page section | Here |
| --- | --- |
| `parse` of scalars, collections, multi-line strings, comments, directives | CYAML, YAML 1.2 core schema |
| several documents | an array of them |
| anchors and aliases | an alias is the same JS value as its anchor, a cycle is allowed |
| tags `!!str`, `!!int`, `!!float`, `!!bool`, `!!null` | as Bun: they type a plain scalar, a quoted one stays a string |
| merge keys `<<: *defaults` | expanded after parsing (a mapping or a sequence of mappings; the first source wins, the mapping's own keys win over all) |
| `SyntaxError` for invalid YAML | `SyntaxError`, message `yaml.parse: ... (line N, column M)` |
| `parse(x)` of a non-string | a buffer is decoded, anything else is coerced with `String()` |
| `stringify(value, null, space)` | flow style without `space`, block style with a number 1-10 or a string (first 10 characters); a replacer or a BigInt throws `Error`; undefined, functions and symbols are skipped |
| `import x from './a.yaml'`, `.yml`, dynamic `import()` | a load hook of `lib/bun.js`: the parsed value is `default`, each top-level key that is an identifier (reserved words such as `class` included) is a named export |

Differences: a circular value throws `TypeError` where Bun writes an anchor (`&root {o: *root}`); the key order of a mapping with
`<<` can differ from Bun's; Bun writes an anchor and an alias for an object that occurs twice (`{a: &a {x: 1},b: *a}`), `stringify` writes it twice; Bun writes a symbol key by its description, `stringify` skips it; `require('./a.yaml')`, `bun --hot` reloading and the bundler (build-time parsing) do not exist here.

## Utilities

Each loads its module on first read, so they cost nothing at startup. Results were compared with
bun 1.4.2 (`tests/unittests/test-bun-util.js` runs under both: `bun tests/unittests/test-bun-util.js`).

| Member | From | Notes |
| --- | --- | --- |
| `env`, `argv`, `main` | `process` | `main` is `process.argv[1]` |
| `version`, `revision`, `origin`, `isMainThread`, `enableANSIColors` | | `version` is qjsm's (`process.versions.qjsm`), `revision` and `origin` are `''` |
| `password.hash/verify/hashSync/verifySync` | [password](password.md) | argon2id (default, needs qjs-lws) and bcrypt; `argon2i`/`argon2d` throw |
| `JSONL.parse/parseChunk` | `lib/jsonl.js` | |
| `JSON5.parse/stringify` | [json5](json5.md) | |
| `JSONC.parse` | [jsonc](jsonc.md) | single quotes, hex numbers and trailing text are a `SyntaxError` |
| `ArrayBufferSink` | [arraybuffer-sink](../native/arraybuffer-sink.md) | |
| `mmap(path, { shared })` | [mmap](../native/mmap.md) | opens read-write like Bun, `shared: false` is a private copy-on-write mapping, an empty file is `EINVAL` |
| `deepEquals(a, b, strict)`, `deepMatch(subset, v)` | `lib/bun/util.js` | Bun's rules: `0` and `-0` differ, `undefined` properties count only when `strict`, Sets compare unordered |
| `inspect(v, opts)`, `inspect.custom` | [inspect](../native/inspect.md) | no colors unless asked; `custom` is `Symbol.for('nodejs.util.inspect.custom')`; no `inspect.table` |
| `Glob` (`match`, `scan`, `scanSync`) | [glob](glob.md) | |
| `which(cmd, { PATH, cwd })` | | executable regular files, `null` when none |
| `sleep(ms \| Date)`, `sleepSync(ms)`, `nanoseconds()`, `gc()` | timers, `os.sleep`, `perf_hooks`, `std.gc` | |
| `peek(p)`, `peek.status(p)` | `misc.promiseState` | |
| `concatArrayBuffers(list, max, asUint8Array)`, `allocUnsafe(n)`, `indexOfLine(buf, offset)` | | |
| `escapeHTML(v)`, `stripANSI(s)`, `stringWidth(s, { countAnsiEscapeCodes })` | [terminal](terminal.md) | wide characters count 2 |
| `readableStreamTo{Text,JSON,Bytes,ArrayBuffer,Blob,Array}` | | string and byte chunks alike |
| `fileURLToPath`, `pathToFileURL` | [url](url.md) | |

| `spawn(cmd, options)`, `spawnSync()` | [bun-spawn](bun-spawn.md) | `Subprocess` over `node/child_process` and `stream` |

Not yet: `$`, `Archive`, `XML`, `semver`, `color`, the hashing and
compression functions (TODO.md Tier 19).

## Globals from other modules

Bun is Node-compatible, so `lib/bun.js` imports [node](node.md) and has every global listed there
(`fetch`, `URL`, `Buffer`, `Intl`, ...). It adds only the `Bun` object, with two members loaded on first
read from qjs-lws (the sibling project `../qjs-lws/`) when it is in the module path:

| Member | From |
| --- | --- |
| `Bun.serve` | `serve` |
| `Bun.fetch` | `fetch`, the same function as the global `fetch` |

- Without qjs-lws both are absent: `Bun.serve` is `undefined`.
- `-B` (`--no-bun`) makes qjsm load `node` alone, `-N` (`--no-node`) neither.
- `Bun.serve` and `Bun.fetch` are on the `Bun` object only, not named exports of `bun`.

## Export list

Generated by `utilities/doc-api.js` from the `bun` dump of `utilities/api-surface.sh`: 54 members. Edit the text above this block, not the block.

**Functions:** `file()`, `mmap()`, `plugin()`, `write()`

**Values:** `stderr`, `stdin`, `stdout`

### `YAML`

object

- **static methods:** `parse()`, `stringify()`

### `default`

object

- **static methods:** `CryptoHasher()`, `Glob()`, `allocUnsafe()`, `argv()`, `concatArrayBuffers()`, `deepEquals()`, `deepMatch()`, `enableANSIColors()`, `env()`, `escapeHTML()`, `file()`, `fileURLToPath()`, `gc()`, `indexOfLine()`, `inspect()`, `isMainThread()`, `main()`, `mmap()`, `nanoseconds()`, `origin()`, `password()`, `pathToFileURL()`, `peek()`, `plugin()`, `readableStreamToArray()`, `readableStreamToArrayBuffer()`, `readableStreamToBlob()`, `readableStreamToBytes()`, `readableStreamToJSON()`, `readableStreamToText()`, `revision()`, `sleep()`, `sleepSync()`, `spawn()`, `spawnSync()`, `stringWidth()`, `stripANSI()`, `version()`, `which()`, `write()`
- **static values:** `stderr`, `stdin`, `stdout`

