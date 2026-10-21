# terminal

Source: `lib/terminal.js` (wraps the `misc` tty functions)

Terminal control for scripts and for the other `lib/` modules (`readline`, `repl`, `tty`,
`process`, `console`): escape sequences, raw mode, window size, key decoding, cell widths and a
buffered `Screen`.

Every function that writes takes the std FILE object (or fd) it writes to as its first argument
`f` (`std.out`, `std.err`, a file from `std.open`), and sends one string through
`misc.ttyWrite(f, s)`. That call flushes `f` first, so the output stays in order with what was
written to `f` before. On a Windows console without VT processing it carries out the sequences
with console API calls (see [misc](../native/misc.md#terminal--console)), so the same code works
there. The module has no default output.

```js
import * as std from 'std';
import { cursorPosition, eraseInLine, windowSize } from 'terminal';

cursorPosition(std.out, 1, 1);
eraseInLine(std.out, 0);
const [columns, rows] = windowSize(std.out);
```

## Exports

### Native, from misc

`ttyWrite(f, data)`, `ttyRead(fd, buffer, offset, length)`, `ttySetRaw(fd, restore)`,
`ttyGetWinSize(fd)`, `EAGAIN`. See [misc](../native/misc.md#terminal--console).

### Cursor movement

`cursorHome(f)`, `cursorUp(f, n)`, `cursorDown(f, n)`, `cursorForward(f, n)`,
`cursorBackward(f, n)`, `cursorNextLine(f, n)`, `cursorPreviousLine(f, n)`,
`cursorHorizontalAbsolute(f, n)`, `cursorPosition(f, row, column)` (1-based),
`cursorOrigin(f)`, `cursorSave(f)` / `cursorRestore(f)` (`ESC 7` / `ESC 8`), `cursorQuery(f)`,
`cursorHide(f)`, `cursorShow(f)`.

### Screen / line editing

`eraseInDisplay(f, n)`, `eraseInLine(f, n)` (n: 0 to the end, 1 to the start, 2 all),
`scrollUp(f, n)`, `scrollDown(f, n)`, `setAlternateScreen(f)`, `setNormalScreen(f)`,
`setScreen(f, alternate)`, `linewrapEnable(f)`, `linewrapDisable(f)` (`CSI ?7 h/l`),
`setScrollRegion(f, top, bottom)`, `resetScrollRegion(f)`.

### Backbuffer

`class Screen(f)` - accumulates draw calls (`write`, `moveTo`, `clear`, `clearLine`, `fg`, `bg`,
`sgr`, `resetAttrs`, `fillRect`, `box`) into an in-memory string and sends them to `f` in one
`flush()`, which avoids flicker on a full-screen redraw. Later draws in a frame cover earlier
ones. `BOX_SINGLE`, `BOX_DOUBLE` are the border sets for `box()`, `centeredRect(cols, rows,
width, height)` places a centered panel.

### Color

`sgr(f, ...codes)`, `rgbForeground(f, r, g, b)`, `rgbBackground(f, r, g, b)`,
`reverseVideoOn(f)`, `reverseVideoOff(f)`, `hslToRgb(h, s, l)`.

### Mouse / device / tabs

`mousetrackingEnable(f)`, `mousetrackingDisable(f)`, `devicecodeQuery(f)`,
`devicestatusQuery(f)`, `deviceReset(f)`, `tabSet(f)`, `tabClear(f)`, `tabsClearall(f)`.

### Input and size

| Export | Description |
| --- | --- |
| `enableRawMode(fd)` / `disableRawMode(fd)` | `ttySetRaw(fd)` / `ttySetRaw(fd, true)`; `enableRawMode` returns whether the output does VT itself |
| `windowSize(f)` | `[columns, rows]` of the window behind a FILE or fd, `[80, 24]` when it is no terminal |
| `readKey(fd)` | one keypress, waits for it: `{ type }` with `up`, `down`, `left`, `right`, `pageup`, `pagedown`, `space`, `enter`, `backspace`, `ctrlc`, `escape` (also any other sequence), `eof`, or `{ type: 'char', char }` |
| `decodeKeys(str, final)` | `{ keys, rest }`: each key as `{ s, key }` with Node's keypress shape `{ sequence, name, ctrl, meta, shift }`; `rest` is an unfinished escape sequence to prepend to the next chunk, unless `final` |

### Widths

`charWidth(ch)` (0 for controls and combining marks, 2 for wide characters, else 1),
`stringWidth(str)` (cells, CSI sequences left out), `stripAnsi(str)` (CSI sequences removed).
