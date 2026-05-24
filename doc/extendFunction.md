# extendFunction

Source: `lib/extendFunction.js` (pure JS) — default export: `extendFunction`

Installs extra (non-enumerable) methods onto `Function.prototype`.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendFunction(proto=Function.prototype)` | 0–1 | function | Defines the extension methods. **(default export)** |
| `FunctionExtensions` | — | const | The bag of methods that get installed. |
| `FunctionPrototype`, `Function` | — | const | The prototype and its constructor. |

## Added `Function.prototype` methods

| Method | Description |
| --- | --- |
| `then`, `catch`, `finally` | Promise-style wrappers around the function's result. |
| `indirect()` | Calls the function indirectly (detached `this`). |
| `bindArguments(...)` | Binds specific argument positions. |
| `bindArray(arr)` | Binds an array of arguments. |
| `bindThis(thisArg)` | Binds only the receiver. |
