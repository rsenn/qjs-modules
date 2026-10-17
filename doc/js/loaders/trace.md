# loaders/trace

Source: `lib/loaders/trace.js` (pure JS, needs the qjsm global `registerHooks`) — exports: `trace`, default `trace`

A tracer for the module loader built on `registerHooks()` (`doc/qjsm.md`): it
logs every `resolve` and `load` that reaches the hook chain and passes each call
on unchanged. It is the JS counterpart of `DEBUG=modules`, which stays in C
because it also sees what happens before any hook exists.

```js
import { trace } from 'loaders_trace';

const t = trace();                                    // lines on stderr
const u = trace({ log: l => lines.push(l), filter: /^file:/ });
t.deregister();                                       // stop tracing
```

| Option | Meaning |
| --- | --- |
| `log(line)` | receives each line; default `console.error` |
| `filter` | `RegExp`; only specifiers (resolve) and urls (load) that match are traced |

Lines:

```
resolve  ./a.js <- file:///work/main.js   => file:///work/a.js
resolve  ./gone.js <- file:///work/main.js   FAIL could not load module '/work/gone.js'
load     file:///work/a.js (module)
```

`trace()` returns the `registerHooks()` handle (`deregister()`, `[Symbol.dispose]`).

## Notes

- Hooks run last-registered first: register the tracer last to see what earlier
  hooks (`node:` prefix, `package.json` aliases, `Bun.plugin`) turned a request into.
- Imports of builtins and of their dependencies are not seen (`doc/qjsm-layout.md` 4.1);
  start `qjsm` with `--hooks-on-builtins` to see the bare-name lookup of user code.
- Nothing here exists in Node, Bun or Deno; use their own loader tracing
  (`NODE_DEBUG=esm`, `--inspect`) there.
