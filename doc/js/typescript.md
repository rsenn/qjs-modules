# typescript

Source: `lib/typescript.js` (JS wrapper over `registerHooks()`) — default export: `installTypeScript()`; qjsm only

Loads `.ts`, `.tsx` and `.mts` modules (format `module-typescript`) by running the
external `swc` CLI (`npm i -g @swc/cli @swc/core`) through `std.popen`. Types are
stripped, not checked; output target is es2022.

```js
import installTypeScript from 'typescript';

installTypeScript();
const { A } = await import('./a.ts');
await import('./a');          // extensionless: tries .ts, .tsx, .mts, /index.ts
```

| Behavior | Detail |
| --- | --- |
| Command | `swc -C jsc.target=es2022 -C jsc.parser.syntax=typescript [-C jsc.parser.tsx=true] <file>` |
| Errors | `Error: swc failed to transpile '<file>'` when swc exits non-zero or prints nothing. |
| Not covered | `.cts` / `commonjs-typescript`. |

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `installTypeScript()` | function | Registers the resolve/load hooks; returns the `registerHooks()` result (`deregister()`). **(default export, also named)** |
