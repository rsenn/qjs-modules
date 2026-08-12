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

## Options

### compact

Controls how objects and arrays are formatted. Accepts positive integers, negative integers, or booleans.

**Positive values** (count-based): Objects with ≤ N entries are rendered on a single line.

```js
inspect({ a: 1, b: 2, c: 3, d: 4 }, { compact: 5 })
// '{ a: 1, b: 2, c: 3, d: 4 }'

inspect({ a: 1, b: 2, c: 3, d: 4, e: 5, f: 6 }, { compact: 5 })
// '{
//    a: 1,
//    b: 2,
//    ...
//  }'
```

**Negative values** (depth-based from leaves): Compact the N deepest leaf levels, regardless of entry count. A "leaf" is an object/array containing only primitives (no nested objects/arrays).

```js
const obj = {
  name: 'lib.a:main.o',
  sections: [
    { name: '.text', size: 256, offset: 64 },
    { name: '.data', size: 0, offset: 320 }
  ]
};

// compact: 1 — sections stays expanded (3 entries > 1)
inspect(obj, { compact: 1 })
// '{
//    name: 'lib.a:main.o',
//    sections: [
//      {
//        name: '.text',
//        size: 256,
//        offset: 64
//      },
//      ...
//    ]
//  }'

// compact: -1 — sections compacts (it's a leaf: contains only primitives)
inspect(obj, { compact: -1 })
// '{
//    name: 'lib.a:main.o',
//    sections: [ { name: '.text', size: 256, offset: 64 }, { name: '.data', size: 0, offset: 320 } ]
//  }'
```

**Boolean values:**
- `true` — Always compact (equivalent to very large positive number)
- `false` — Never compact (fully expanded)

Default: `5`
