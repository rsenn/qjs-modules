# loaders/commonjs

Source: `lib/loaders/commonjs.js` (JS wrapper over `registerHooks()` and `lib/require.js`) — default export: `installCommonJS()`; qjsm only

Loads CommonJS modules, chosen the way Node chooses them. [node](../node.md) installs it at startup.

| File | Loaded as |
| --- | --- |
| `.cjs` | CommonJS |
| `.js`, nearest `package.json` has `"type": "commonjs"` | CommonJS |
| `.js`, nearest `package.json` has `"type": "module"` | ES module |
| `.js`, no `"type"` (or no `package.json`) | CommonJS, unless it only parses as an ES module (Node 22.7+ syntax detection) |
| any `load` result with `format: 'commonjs'` | CommonJS |

Installing it also sets `globalThis.require` (the
`require()` of `lib/require.js`); `deregister()` removes it again.

```js
import installCommonJS from 'loaders/commonjs';

installCommonJS();
const { sum } = await import('./lib.cjs');   // lib.cjs: exports.sum = require('./dep.cjs').v + 1
```

| Behavior | Detail |
| --- | --- |
| Execution | The module runs at load time with `exports`, `require`, `module`, `__filename`, `__dirname`; `require()` resolves relative to the module, a bare name through the `node_modules` above it, and a Node name (`fs`, `util`, `node:fs`, ...) to Node's module object; an error thrown while loading reaches the caller unchanged. |
| Result | An ES module: `default` is `module.exports`, plus one named export per own key that is a valid identifier. |
| Scope | Sees results of hooks registered before it; a hook registered later that short-circuits with `format: 'commonjs'` bypasses it. |
| Not covered | `require()` is one shared function (BUGS commonjs-require-not-per-module); frames show `<input>` and lines 2 too high (BUGS commonjs-stack-has-no-file-and-line). |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installCommonJS()` | function | Registers the resolve/load hooks; returns the `registerHooks()` result (`deregister()`).**(default export)** |
