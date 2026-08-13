# extendAsyncGenerator

Source: `lib/extendAsyncGenerator.js` (pure JS) — default export: `extendAsyncGenerator`

Installs extra methods onto the async-generator prototype.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `extendAsyncGenerator(proto=AsyncGeneratorPrototype)` | 0–1 | function | Installs the extensions. **(default export)** |
| `AsyncGeneratorExtensions` | — | const | The bag of methods that get installed. |
| `AsyncGeneratorPrototype`, `AsyncGenerator` | — | const | The async-generator prototype and its constructor. |

## Added async-generator methods

| Method | Description |
| --- | --- |
| `includes(value)` | Whether the async stream yields `value`. |
| `enumerate()` | Yields `[index, value]` pairs. |
