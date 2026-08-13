# extendMath

Source: `lib/extendMath.js` (pure JS) — default export: `extendMath`

Installs extra functions onto the `Math` object.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendMath(ctor=Math)` | 0–1 | function | Installs the extensions. **(default export)** |
| `MathExtensions` | — | const | The bag of functions that get installed. |

## Added `Math` functions

| Function | Description |
| --- | --- |
| `exp10(x)` | `10 ** x`. |
| `mantissa(x)` | Mantissa of a float. |
| `exponent(x)` | Exponent of a float. |
| `fsign(x)` | Sign bit of a float. |
| `float64(...)` | Float64 bit-level helper. |
