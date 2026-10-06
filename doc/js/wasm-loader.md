# wasm-loader

Source: `lib/wasm-loader.js` (JS wrapper over `registerHooks()`) — default export: `installWasmLoader()`; qjsm only

Loads `.wasm` files as ES modules (format `module-wasm`), the instance phase of the
WebAssembly ESM integration. Opt-in: nothing is registered until it is called.

```js
import installWasmLoader from 'wasm-loader';

installWasmLoader();
const { add } = await import('./add.wasm');
```

| Behavior | Detail |
| --- | --- |
| Imports | each wasm import module name is a specifier resolved like a JS import (`"./env.js"`, `"env"`) and evaluated first |
| Exports | every export becomes a named export; a global export holds its value, memories, tables and functions are the `wasm` objects |
| Bytes | read again from the file at evaluation; the `.wasm` URL must be a `file:` path |
| Not covered | `import source` (engine lacks it), `with { type: 'wasm' }` (the hooks' `importAttributes` is always empty) |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installWasmLoader()` | function | Registers the resolve/load hooks; returns the `registerHooks()` result (`deregister()`). **(default export, also named)** |
