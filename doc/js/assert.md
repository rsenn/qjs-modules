# assert

Source: `lib/assert.js` (pure JS) — default export: `assert`

Node `assert`-style assertion helpers. Each throws an `AssertionError` on
failure; `message` is optional.

## Exports

| Export | Args | Description |
| --- | --- | --- |
| `assert(cond, message)` | 2 | Throws unless `cond` is truthy. **(default export)** |
| `AssertionError` | — | `Error` subclass thrown by the assertions. |
| `ok(value, message)` | 2 | Alias of `assert`. |
| `equal(actual, expected, message)` | 3 | `==` equality. |
| `notEqual(actual, expected, message)` | 3 | `!=` inequality. |
| `strictEqual(actual, expected, message)` | 3 | `===` equality. |
| `notStrictEqual(actual, expected, message)` | 3 | `!==` inequality. |
| `deepEqual(actual, expected, message)` | 3 | Deep `==` equality. |
| `notDeepEqual(actual, expected, message)` | 3 | Deep inequality. |
| `deepStrictEqual(actual, expected, message)` | 3 | Deep strict equality. |
| `notDeepStrictEqual(actual, expected, message)` | 3 | Deep strict inequality. |
| `partialDeepStrictEqual(actual, expected, message)` | 3 | `expected` is a deep subset of `actual`. |
| `match(string, regexp, message)` | 3 | `regexp` matches `string`. |
| `doesNotMatch(string, regexp, message)` | 3 | `regexp` does not match `string`. |
| `throws(fn, error, message)` | 3 | `fn()` throws (optionally matching `error`). |
| `doesNotThrow(fn, error, message)` | 3 | `fn()` does not throw. |
| `rejects(asyncFn, error, message)` | 3 | The returned promise rejects. |
| `doesNotReject(asyncFn, error, message)` | 3 | The returned promise resolves. |
| `ifError(value)` | 1 | Throws if `value` is truthy (an error). |
| `fail(message)` | 1 | Always throws. |
