# Runtime compatibility: qjsm against Node, Bun and Deno

Measured 2026-11-03 with `qjsm` built from `b17790ce`, against node 23.11.1, bun 1.4.2 and deno 2.9.7.
Each cell counts the members (flat names, nested ones such as `posix.join` included) that the runtime
exports **out of the members node exports**, as listed by `utilities/describe.sh --flat --no-arguments`.
Names only: a method with a different behaviour still counts, behaviour is tested by the `@portable`
tests (`tests/unittests/test-*.js` also run under node, bun and deno).

Reproduce one row:

```sh
for rt in qjsm node bun deno; do utilities/describe.sh -r $rt --flat --no-arguments node:events > /tmp/events.$rt; done
```

## Node modules

| Module | node | qjsm | bun | deno |
| --- | ---: | ---: | ---: | ---: |
| `node:fs` | 258 | 168 | 253 | 228 |
| `node:fs/promises` | 91 | 72 | 91 | 89 |
| `node:path` | 52 | 31 | 52 | 52 |
| `node:util` | 107 | 105 | 100 | 103 |
| `node:events` | 81 | 49 | 77 | 81 |
| `node:buffer` | 145 | 106 | 138 | 142 |
| `node:child_process` | 16 | 13 | 15 | 15 |
| `node:os` | 29 | 29 | 29 | 29 |
| `node:url` | 72 | 33 | 70 | 55 |
| `node:assert` | 31 | 23 | 31 | 31 |
| `node:timers` | 16 | 7 | 15 | 12 |
| `node:timers/promises` | 6 | 5 | 5 | 5 |
| `node:perf_hooks` | 111 | 36 | 98 | 30 |
| `node:readline` | 46 | 13 | 46 | 46 |
| `node:string_decoder` | 9 | 5 | 9 | 8 |
| `node:module` | 173 | 157 | 150 | 140 |
| `node:process` | 481 | 247 | 270 | 247 |
| `node:wasi` | 6 | 6 | 4 | 6 |

`node:zlib`, `node:crypto`, `node:net`, `node:http` are not provided by qjsm. `node:stream` is not provided
on purpose: Node's `stream.Readable`/`Writable`/`Duplex`/`Transform` are a second stream model; the WHATWG
`ReadableStream`, `WritableStream` and `TransformStream` of [stream](js/stream.md) are the streams here.

What the top-level misses are (qjsm against node, from the same dump):

| Module | Missing top-level exports |
| --- | --- |
| `node:fs` | `F_OK R_OK W_OK X_OK` (the constants are in `fs.constants`), `ReadStream`, `WriteStream`, `FileReadStream`, `FileWriteStream`, `fchmod`, `fchown`, `futimes`, `lchmod`, `lchown`, `lutimes`, `glob`, `opendir`, `statfs`, `watchFile`, `unwatchFile` (sync and callback forms) |
| `node:fs/promises` | `glob`, `lchmod`, `statfs` |
| `node:path` | `matchesGlob`, `win32` |
| `node:events` | `EventEmitterAsyncResource`, `addAbortListener`, `captureRejections`, `errorMonitor` |
| `node:url` | `URLPattern`, the legacy `Url`, `parse`, `format`, `resolve`, `domainToASCII`, `domainToUnicode`, `urlToHttpOptions` |
| `node:assert` | `CallTracker` |
| `node:buffer` | `File`, `resolveObjectURL`, `transcode` |
| `node:perf_hooks` | `createHistogram`, `monitorEventLoopDelay`, `PerformanceResourceTiming` |
| `node:timers` | `promises` property and the internal `active`, `enroll`, `unenroll` |

## Globals

| Global | node | qjsm | bun | deno |
| --- | ---: | ---: | ---: | ---: |
| `process` | 86 | 69 | 80 | 74 |
| `Buffer` | 112 | 93 | 109 | 111 |
| `URL` | 22 | 15 | 22 | 22 |
| `URLSearchParams` | 18 | 15 | 18 | 18 |
| `AbortController` | 7 | 5 | 5 | 6 |
| `AbortSignal` | 12 | 10 | 10 | 12 |
| `Blob` | 12 | 11 | 10 | 11 |
| `File` | 7 | 5 | 5 | 6 |
| `TextEncoder` | 7 | 6 | 6 | 7 |
| `TextDecoder` | 8 | 7 | 7 | 8 |
| `EventTarget` | 9 | 6 | 6 | 7 |
| `ReadableStream` | 14 | 12 | 13 | 14 |
| `WritableStream` | 9 | 7 | 8 | 9 |
| `TransformStream` | 7 | 5 | 6 | 7 |
| `WebAssembly` | 18 | 15 | 18 | 18 |
| `Intl` | 14 | 11 | 14 | 14 |
| `console` | 34 | 6 | 27 | 27 |
| `performance` | 2 | 1 | 1 | 2 |

`structuredClone`, `crypto`, `fetch`, `queueMicrotask`, `setTimeout` exist everywhere. `fetch`, `URL`,
`FormData` and the other network globals come from `qjs-lws` when it is in the module path (see
[node](js/node.md) and [globals](js/globals.md)); the count for `URL` above is the standalone `url` module.

## Bun

`Bun` has 115 top-level members in bun 1.4.2 and 46 in qjsm, all 46 of qjsm's also in bun.
Provided: `file()`, `write()`, `stdin`, `stdout`, `stderr`, `mmap()`, `plugin()`, `Glob`, `JSON5`, `JSONC`, `JSONL`,
`ArrayBufferSink`, `password`, `inspect()`, `deepEquals()`, `deepMatch()`, `which()`, `sleep()`, `sleepSync()`,
`peek()`, `nanoseconds()`, `stringWidth()`, `stripANSI()`, `escapeHTML()`, `concatArrayBuffers()`, `allocUnsafe()`,
`indexOfLine()`, `readableStreamTo{Array,ArrayBuffer,Blob,Bytes,JSON,Text}()`, `fileURLToPath()`, `pathToFileURL()`,
`gc()`, `env`, `argv`, `main`, `isMainThread`, `version`, `revision`, `origin`, and `serve`/`fetch` when
qjs-lws is in the module path. See [bun](js/bun.md) and [bun-util](js/bun-util.md).

Not provided, among others: `Bun.$`, `spawn()`/`spawnSync()` (use `child_process`), `sql()`/`SQL`, `build()`,
`S3Client`, `RedisClient`, `CryptoHasher`/`hash()`, `gzipSync()` and the other compression functions, `TOML`,
`YAML`, `XML`, `semver`, `Transpiler`, `FFI`, `cron()`, `color()`.

## Deno

Deno is a target of the `@portable` tests for the shared Node-style surface (through
`tests/deno-import-map.json`). `Deno.*` itself is not implemented.

## Related

- [api-compatibility.md](api-compatibility.md) — module-by-module standards classification
- [api-compatibility-nodejs.md](api-compatibility-nodejs.md) — Node API gaps in detail
- [api-compare-json.md](api-compare-json.md), [api-compare-path.md](api-compare-path.md), [api-compare-xml.md](api-compare-xml.md) — per-module comparisons
