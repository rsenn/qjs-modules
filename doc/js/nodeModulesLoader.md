# nodeModulesLoader

Source: `lib/nodeModulesLoader.js` (pure JS) — default export: a factory function

Example custom `moduleLoader()` hook installer: `node_modules`/`package.json`-aware
resolution (`"exports"`/`"module"`/`"main"` field lookup), plus an experimental `.ts`
transpile-via-`swc` loader. Demonstrates the `moduleLoader()` hook API - not invoked
automatically by anything else in this project; call the default export to install it.

Was formerly named `module`/`lib/module.js`, which shadowed the standard `module`
specifier (Node's `node:module` API - see `doc/js/module.md`). Renamed so both names
can coexist.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| *(default)* | function | Installs the `node_modules`/`package.json`/`.ts` loader hooks via `moduleLoader()`. |
