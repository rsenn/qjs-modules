# vfs

Source: `lib/vfs.js` (pure JS; `ArchiveProvider` needs the native [`archive`](../native/archive.md) module)

Virtual file systems with a `node:fs`-like API, modelled on Node's `node:vfs`
(26.4+, experimental). `import vfs from 'node:vfs'` resolves here under qjsm
(`nodeHooks`).

```js
import vfs from 'node:vfs';

const v = vfs.create();                    // MemoryProvider
v.mkdirSync('/dir', { recursive: true });
v.writeFileSync('/dir/a.txt', 'hi');
v.readFileSync('/dir/a.txt', 'utf8');      // 'hi'
await v.promises.readdir('/dir');          // ['a.txt']
```

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `create(provider?, options?)` | function | `new VirtualFileSystem(provider)`; default provider is a `MemoryProvider`. |
| `VirtualFileSystem` | class | The fs-like API over one provider. |
| `VirtualProvider` | class | Base class of providers; subclass it for a custom file system. |
| `MemoryProvider` | class | Files, directories, symlinks in memory; `setReadOnly()`. |
| `RealFSProvider(root)` | class | A real directory as the root. A path mapping, not a sandbox. |
| `ComposableProvider(providers)` | class | Layers providers: first is writable, reads fall through, removals hide lower copies. |
| `ArchiveProvider(file)` | class | Read-only view of a tar/zip/... file (not in `node:vfs`). |
| `default` | object | All of the above, as `node:vfs`'s default export. |

## VirtualFileSystem

Paths are POSIX and absolute (`..` stops at `/`); relative paths are taken from `/`.
Errors are `SyscallError` with `code` and `path`. Without an encoding `readFileSync`
returns a `Uint8Array`.

| Group | Members |
| --- | --- |
| sync | `existsSync statSync lstatSync readFileSync writeFileSync appendFileSync readdirSync mkdirSync rmdirSync unlinkSync renameSync copyFileSync realpathSync readlinkSync symlinkSync accessSync rmSync truncateSync chmodSync utimesSync mkdtempSync` |
| fd | `openSync closeSync readSync writeSync fstatSync fsyncSync ftruncateSync`; fds are numbers >= 10000, buffered and written to the provider on `closeSync`/`fsyncSync` or before the next path call |
| callback | `readFile(path, cb)` etc. for every name above that has a callback form in Node (`access appendFile chmod close copyFile fstat ftruncate lstat mkdir mkdtemp open read readFile readdir readlink realpath rename rm rmdir stat symlink truncate unlink utimes write writeFile`) |
| promises | `vfs.promises.*`, the same names minus the fd ones |
| props | `provider`, `readonly` |

`readdirSync` takes `{ withFileTypes, recursive }`; `mkdirSync` takes `{ recursive }`
and returns the first directory created; `statSync` takes `{ throwIfNoEntry }`.

## Writing a provider

A provider receives absolute, normalized paths and implements path primitives;
everything else (`exists`, `access`, `append`, `copyFile`, `truncate`, `mkdtemp`,
`rm`, `realpath`) is derived by the base class:

| Primitive | Returns / does |
| --- | --- |
| `statSync(path)` | `fs.Stats` (follows symlinks) |
| `readdirSync(path)` | array of names |
| `readFileSync(path)` | `Uint8Array` with the whole file |
| `writeFileSync(path, bytes, mode?)` | create or replace |
| `mkdirSync(path, recursive)`, `rmdirSync`, `unlinkSync`, `renameSync(from, to)` | as in `fs` |
| optional | `lstatSync readlinkSync symlinkSync chmodSync utimesSync`, getters `readonly`, `supportsSymlinks` |

A primitive left out throws `ERR_METHOD_NOT_IMPLEMENTED`, or `EROFS` when the
provider is `readonly`.

## Not implemented

`mount()`/`unmount()` (redirecting `fs`, `require`, `import`), `createReadStream`/
`createWriteStream`, `watch`, `opendir`, `openAsBlob`, `registerProvider`.
Providers differ from Node's: whole-file `readFileSync`/`writeFileSync` instead
of `open`, so a custom provider is not portable to `node:vfs`.
