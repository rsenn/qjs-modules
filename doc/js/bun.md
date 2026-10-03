# bun

Source: `lib/bun.js` (pure JS, no imports) — named exports: `plugin`; also installed as `globalThis.Bun = { plugin }` by qjsm

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
| `typeof Bun` is the Bun runtime | `Bun` exists only to carry `plugin`; do not use it to detect Bun |
| Plugins in `bunfig.toml` `preload` | Call `Bun.plugin()` before the `import()` it should affect (static imports are resolved first) |
