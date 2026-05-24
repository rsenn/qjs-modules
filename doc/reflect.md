# reflect

Source: `lib/reflect.js` (pure JS)

Reflection / (de)serialization helpers that encode JS values into a descriptive
form and back, plus prototype utilities.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `hasPrototype(obj, ...proto)` | 1+ | function | Whether any of `proto` is in `obj`'s prototype chain. |
| `makeFunctionWithArgs(code, args)` | 1–2 | function | Builds a `Function` from `code` and parameter names. |
| `getKeys(obj, t)` | 1–2 | generator | Yields keys of `obj` filtered by predicate `t(desc, key)`. |
| `EncodeJS(val, stack, mapFn)` | 1–3 | function | Encodes a JS value into a serializable descriptor. |
| `EncodeObj(obj, keys, stack, mapFn)` | 1–4 | function | Encodes an object's selected `keys`. |
| `DecodeJS(info)` | 1 | function | Reconstructs a JS value from an `EncodeJS` descriptor. |
| `TypedArray`, `TypedArrayPrototype` | — | const | The shared `TypedArray` base and its prototype. |
| `JSValue`, `JSNumber`, `JSString`, `JSBoolean`, `JSSymbol`, `JSRegExp`, `JSObject`, `JSArray`, `JSTypedArray`, `JSFunction`, `JSProperty`, `JS` | — | re-export | Value-wrapper types used by the encoder. |
