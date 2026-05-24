# extendObject

Source: `lib/extendObject.js` (pure JS) — default export: `extendObject`

Installs extra static helpers onto the `Object` constructor.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendObject(proto=Object.prototype, ctor=Object)` | 0–2 | function | Installs the extensions. **(default export)** |
| `ObjectStatic` | — | const | The bag of static helpers installed on `Object`. |
| `ObjectExtensions` | — | const | The (prototype-level) extension bag. |

## Added `Object` static helpers

`getMemberNames`, `getMemberSymbols`, `getMethodNames`, `getMethodSymbols`,
`getPropertyNames`, `getPropertySymbols`, `getPropertyDescriptor`,
`getPropertyDescriptors` — each walks the prototype chain to collect
names/symbols/descriptors.
