# inspect

Source: `quickjs-inspect.c` — module exports `inspect` (also the `default` export).

A Node-`util.inspect`-style pretty printer for arbitrary JS values, with depth,
color, and formatting options. Handles cycles, typed arrays, Maps/Sets, classes
and custom inspect hooks.

## Functions

| Function | Args | Description |
| --- | --- | --- |
| `inspect(value, options)` | 1 | Returns a human-readable string representation of `value`. `options` controls depth, colors, `maxArrayLength`, `maxStringLength`, indentation, etc. |

`inspect` is exported both by name and as the module `default`.
