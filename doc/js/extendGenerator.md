# extendGenerator

Source: `lib/extendGenerator.js` (pure JS) — default export: `extendGenerator`

Installs extra methods onto the generator prototype.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendGenerator(proto=GeneratorPrototype)` | 0–1 | function | Installs the extensions. **(default export)** |
| `GeneratorExtensions` | — | const | The bag of methods that get installed. |
| `GeneratorPrototype`, `Generator` | — | const | The generator prototype and its constructor. |

## Added generator methods

`includes(value)` — and other lazy iteration helpers operating over the
generator's yielded values.
