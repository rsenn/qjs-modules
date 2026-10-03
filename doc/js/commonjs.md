# commonjs

Source: `lib/commonjs.js` (JS wrapper over `registerHooks()` and `lib/require.js`) — default export: `installCommonJS()`; qjsm only

Loads modules whose format is `commonjs`: files ending in `.cjs`, and any `load`
result with `format: 'commonjs'`. Importing it also sets `globalThis.require` (the
`require()` of `lib/require.js`).

```js
import installCommonJS from 'commonjs';

installCommonJS();
const { sum } = await import('./lib.cjs');   // lib.cjs: exports.sum = require('./dep.cjs').v + 1
```

| Behavior | Detail |
| --- | --- |
| Execution | The module runs at load time with `exports`, `require`, `module`, `__filename`, `__dirname`; `require()` resolves relative to the module. |
| Result | An ES module: `default` is `module.exports`, plus one named export per own key that is a valid identifier. |
| Scope | Sees results of hooks registered before it; a hook registered later that short-circuits with `format: 'commonjs'` bypasses it. |
| Not covered | `.js` files in a `package.json` with `"type": "commonjs"` (only `.cjs` is tagged). |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installCommonJS()` | function | Registers the resolve/load hooks; returns the `registerHooks()` result (`deregister()`). **(default export, also named)** |
