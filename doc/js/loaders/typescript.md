# loaders/typescript

Source: `lib/loaders/typescript.js` (JS wrapper over `registerHooks()`) — default export: `installTypeScript()`; qjsm only

Loads `.ts` and `.mts` modules (format `module-typescript`) by stripping their
types with node's `module.stripTypeScriptTypes` (swc in strip-only mode, node
22.13+ in `PATH`), run through `std.popen`. Types are blanked out with spaces,
not checked, so line and column numbers stay exact.

```js
import installTypeScript from 'loaders/typescript';

installTypeScript();
const { A } = await import('./a.ts');
await import('./a');          // extensionless: tries .ts, .mts, /index.ts
```

| Behavior | Detail |
| --- | --- |
| Command | `node -e "<stripTypeScriptTypes(readFileSync(file))>" <file>` |
| Supported | erasable syntax only: annotations, interfaces, type aliases, generics, `as`/`satisfies`, `declare`, `import type` |
| Rejected | `enum`, `namespace` with values, parameter properties, `import x = require()`: the load throws |
| Errors | `Error: cannot strip types of '<file>': <node's error line>`, or `cannot run node for '<file>'` |
| Not covered | `.tsx` (JSX), `.cts` / `commonjs-typescript`. |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installTypeScript()` | function | Registers the resolve/load hooks; returns the `registerHooks()` result (`deregister()`).**(default export)** |
