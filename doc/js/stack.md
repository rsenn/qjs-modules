# stack

Source: `lib/stack.js` (pure JS) — default export: `StackFrame`

Parses and represents call-stack frames.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `Location` | — | re-export | Re-exported from the `location` module. |
| `Stack(st, pred)` | 1–2 | function | Parses a raw stack string into an array of `StackFrame`s, filtered by `pred`. |
| `StackFrame` | — | class | One stack frame: function name, file, line, column. **(default export)** |
