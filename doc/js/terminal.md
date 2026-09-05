# terminal

Source: `lib/terminal.js` (pure JS)

ANSI/VT escape-sequence helpers for terminal control. Each function writes (or
returns) the corresponding escape sequence; most accept an output `f` defaulting
to `process.stdout`.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `terminal` | let | The default output target (`process.stdout`). |

### Cursor movement

`cursorHome(n)`, `cursorUp(n)`, `cursorDown(n)`, `cursorForward(n)`,
`cursorBackward(n)`, `cursorNextLine(n)`, `cursorPreviousLine(n)`,
`cursorHorizontalAbsolute(n)`, `cursorPosition(row, column)`, `cursorOrigin()`,
`cursorSave()`, `cursorRestore()`, `cursorQuery()`.

### Screen / line editing

`eraseInDisplay(n)`, `eraseInLine(n)`, `scrollUp(n)`, `scrollDown(n)`,
`setAlternateScreen()`, `setNormalScreen()`, `setScreen(alternate)`,
`linewrapEnable()`, `linewrapDisable()`, `setScrollRegion(top, bottom, f)`,
`resetScrollRegion(f)`.

### Backbuffer

`class Screen(f)` - accumulates draw calls (`write`, `moveTo`, `clear`,
`clearLine`, `fg`, `bg`, `sgr`, `resetAttrs`) into an in-memory string and
sends them to `f` in one `flush()` call, instead of writing straight to
the tty per call - avoids flicker on a full-screen redraw (e.g. a
scrollable list repainted every keypress).

### Color

`rgbForeground(f, r, g, b)`, `rgbBackground(f, r, g, b)`.

### Mouse / device / tabs

`mousetrackingEnable(f)`, `mousetrackingDisable(f)`, `devicecodeQuery()`,
`devicestatusQuery()`, `deviceReset()`, `tabSet()`, `tabClear()`,
`tabsClearall()`.

### Low-level sequence builders

`numberSequence(f, n, c)`, `numbersSequence(f, numbers, c)`,
`escapeNumberChar(f, n, c)`, `escapeChar(f, c)`, `escapeSequence(f, seq)`,
`commandSequence(f, seq)`, `commandNumberChar(n, c)`, `commandChar(c)`.
