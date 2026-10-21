# terminal

Source: `lib/terminal.js` (pure JS)

ANSI/VT escape-sequence helpers for terminal control. Every function that
writes takes the std FILE object it writes to as its first argument `f`
(`std.out`, `std.err`, a file from `std.open`); the module has no default
output.

```js
import * as std from 'std';
import { cursorPosition, eraseInLine, windowSize } from 'terminal';

cursorPosition(std.out, 1, 1);
eraseInLine(std.out, 0);
const [columns, rows] = windowSize(std.out);
```

## Exports

### Cursor movement

`cursorHome(f)`, `cursorUp(f, n)`, `cursorDown(f, n)`, `cursorForward(f, n)`,
`cursorBackward(f, n)`, `cursorNextLine(f, n)`, `cursorPreviousLine(f, n)`,
`cursorHorizontalAbsolute(f, n)`, `cursorPosition(f, row, column)`,
`cursorOrigin(f)`, `cursorSave(f)`, `cursorRestore(f)`, `cursorQuery(f)`,
`cursorHide(f)`, `cursorShow(f)`.

### Screen / line editing

`eraseInDisplay(f, n)`, `eraseInLine(f, n)` (through `misc.clearScreen` /
`misc.clearLine`, which use the console API on Windows), `scrollUp(f, n)`,
`scrollDown(f, n)`, `setAlternateScreen(f)`, `setNormalScreen(f)`,
`setScreen(f, alternate)`, `linewrapEnable(f)`, `linewrapDisable(f)`,
`setScrollRegion(f, top, bottom)`, `resetScrollRegion(f)`.

### Backbuffer

`class Screen(f)` - accumulates draw calls (`write`, `moveTo`, `clear`,
`clearLine`, `fg`, `bg`, `sgr`, `resetAttrs`, `fillRect`, `box`) into an
in-memory string and sends them to `f` in one `flush()` call, instead of
writing straight to the tty per call - avoids flicker on a full-screen redraw
(e.g. a scrollable list repainted every keypress).

### Color

`rgbForeground(f, r, g, b)`, `rgbBackground(f, r, g, b)`, `sgr(f, ...codes)`,
`reverseVideoOn(f)`, `reverseVideoOff(f)`, `hslToRgb(h, s, l)`.

### Mouse / device / tabs

`mousetrackingEnable(f)`, `mousetrackingDisable(f)`, `devicecodeQuery(f)`,
`devicestatusQuery(f)`, `deviceReset(f)`, `tabSet(f)`, `tabClear(f)`,
`tabsClearall(f)`.

### Input and size

`enableRawMode(fd)`, `disableRawMode(fd)`, `readKey(fd)` take a file
descriptor (the input side); `windowSize(f)` takes the output FILE and
returns `[columns, rows]`, `[80, 24]` when it is not a tty.

### Low-level sequence builders

`numberSequence(f, n, c)`, `numbersSequence(f, numbers, c)`,
`escapeNumberChar(f, n, c)`, `escapeChar(f, c)`, `escapeSequence(f, seq)`,
`commandSequence(f, seq)`, `commandNumberChar(f, n, c)`, `commandChar(f, c)`.
