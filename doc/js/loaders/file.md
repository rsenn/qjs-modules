# loaders/file

Source: `lib/loaders/file.js` (JS wrapper over `registerHooks()`) — default export: `installFileLoader()`; qjsm only

Makes `with { type: 'file' }` imports work as in Bun: the default export is the file's
absolute path, and the file itself is never read or compiled. Opt-in: nothing is
registered until it is called.

```js
import installFileLoader from 'loaders/file';

installFileLoader();
const { default: source } = await import('./hello.c', { with: { type: 'file' } });
// "/abs/dir/hello.c"
```

A static `import source from './hello.c' with { type: 'file' }` works too, in a module
that is loaded after `installFileLoader()` ran.

| Runtime | `with { type: 'file' }` |
| --- | --- |
| Bun | default export is the absolute path (no bundling); namespace has `default` and `__esModule` |
| Node, Deno | unsupported (error) |
| qjsm + this loader | default export is the absolute path; no `__esModule` |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installFileLoader()` | function | Registers the load hook; returns the `registerHooks()` result (`deregister()`). **(default export, also named)** |
