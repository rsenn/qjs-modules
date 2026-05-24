# readline

Source: `lib/readline.js` (pure JS)

Cursor-movement helpers in the style of Node's `readline` module.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `cursorTo(stream, column, row)` | 3 | function | Moves the cursor to an absolute `(column, row)` on `stream`. |
| `clearLine(stream, dir)` | 2 | function | Clears the current line in direction `dir` (-1 left, 1 right, 0 whole line). |
