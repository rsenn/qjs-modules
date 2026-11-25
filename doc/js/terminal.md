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

The module also runs on node, bun and deno (checked 2026-11-19, `test-terminal-portable.js`). There
`f` is `process.stdout`, `process.stderr` or an fd number, `ttyWrite` is `f.write(s)` or
`fs.writeSync`, and raw mode, size and reads go through `process.stdin`, `process.stdout` and
`fs.readSync(0)`. `readKey(0)` blocks by polling every 10 ms on every runtime.

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
`setScrollRegion(f, top, bottom)`, `resetScrollRegion(f)`, `insertLines(f, n)` /
`deleteLines(f, n)` (`CSI n L` / `CSI n M`: rows below the cursor move down or up inside the
scroll region).

`alternateScreen(f, fn)` runs `fn` on the alternate screen and switches back to the normal one
afterwards, also when `fn` throws; when `fn` returns a promise the switch waits for it. The
normal screen, its scrollback and cursor stay untouched meanwhile (a pager, a full-screen
picker):

```js
await alternateScreen(std.out, async () => {
  /* draw, read keys */
});
```

### Backbuffer

`class Screen(f)` - accumulates draw calls (`write`, `moveTo`, `clear`, `clearLine`, `fg`, `bg`,
`sgr`, `resetAttrs`, `fillRect`, `box`) into an in-memory string and sends them to `f` in one
`flush()`, which avoids flicker on a full-screen redraw. Later draws in a frame cover earlier
ones. `alternate(on)` switches screens inside the frame, `lines(row, col, lines)` draws the
lines a widget below returns. `BOX_SINGLE`, `BOX_DOUBLE`, `BOX_ROUNDED`, `BOX_HEAVY` are the
border sets for `box()`, `boxLines()` and `table()` (keys `tl tr bl br h v` and the tees
`t b l r x`); `centeredRect(cols, rows, width, height)` places a centered panel.

### Color

`sgr(f, ...codes)`, `rgbForeground(f, r, g, b)`, `rgbBackground(f, r, g, b)`,
`reverseVideoOn(f)`, `reverseVideoOff(f)`, `hslToRgb(h, s, l)`.

### Mouse / device / tabs

`mouseEnable(f, mode)` turns on SGR mouse reports; `mode` is `click` (presses, releases, wheel,
the default), `drag` (also motion with a button held) or `move` (all motion).
`mouseDisable(f)` turns every mode off. `decodeKeys()` and `readKey()` decode the reports
(SGR `ESC [ < b;x;y M/m`, urxvt `ESC [ b;x;y M` and X10 `ESC [ M bxy`) into mouse keys:

```js
mouseEnable(std.out, 'drag');
readKey(0);   // { type: 'mouse', button: 'left', action: 'press', x: 12, y: 3, ctrl: false, meta: false, shift: false }
```

| Field | Values |
| --- | --- |
| `button` | `left`, `middle`, `right`, `none` (motion without a button, X10 releases), `wheelup`, `wheeldown`, `wheelleft`, `wheelright` |
| `action` | `press`, `release`, `drag`, `move`, `wheel` |
| `x`, `y` | cell, 1-based |

`decodeKeys()` gives such a key as `{ name: 'mouse', mouse: { button, action, x, y }, ctrl,
meta, shift }`. `mousetrackingEnable(f)` / `mousetrackingDisable(f)` set modes 1000, 1006 and
1015 together.

`devicecodeQuery(f)`, `devicestatusQuery(f)`, `deviceReset(f)`, `tabSet(f)`, `tabClear(f)`,
`tabsClearall(f)`.

### Input and size

| Export | Description |
| --- | --- |
| `enableRawMode(fd)` / `disableRawMode(fd)` | `ttySetRaw(fd)` / `ttySetRaw(fd, true)`; `enableRawMode` returns whether the output does VT itself |
| `windowSize(f)` | `[columns, rows]` of the window behind a FILE or fd, `[80, 24]` when it is no terminal |
| `readKey(fd)` | one keypress, waits for it: `{ type }` with `up`, `down`, `left`, `right`, `pageup`, `pagedown`, `space`, `enter`, `backspace`, `ctrlc`, `escape` (also any other sequence), `eof`, `{ type: 'char', char }` or `{ type: 'mouse', ... }` (see Mouse) |
| `decodeKeys(str, final)` | `{ keys, rest }`: each key as `{ s, key }` with Node's keypress shape `{ sequence, name, ctrl, meta, shift }`; `rest` is an unfinished escape sequence to prepend to the next chunk, unless `final` |

### Line editor

`new LineEditor({ history, maxHistory })` is the state of one edited line, with no I/O: feed it the
`key` of each `decodeKeys()` entry and draw `layout()`.

```js
const ed = new LineEditor({ history: ['ls', 'pwd'] });
for(const { key } of decodeKeys(input).keys) {
  const what = ed.key(key); // 'submit' 'cancel' 'eof' 'change' or null (not a key of the editor)
  if(what == 'submit') run(ed.accept()); // the text goes into the history, the editor is empty again
}
const { lines, row, col } = ed.layout(['> ', '. '], columns); // as layoutInput()
```

Keys: printable keys and pastes insert; Backspace, Delete, the arrows, Home and End; Ctrl-A E B F D H K U
W Y; Alt-B F D Backspace and Ctrl-Left/Right move or kill by word; Ctrl-J, Alt-Enter and Shift-Enter
insert a newline; Enter submits, Ctrl-C cancels, Ctrl-D on an empty line is the end of input. Up and
Down move a line in a multi-line text, else through the history; the text typed before the first Up
comes back with Down, and Down in the new entry with text in it puts that text into the history and
starts one empty entry. Enable bracketed paste with `enableCapabilities` (only when
`caps.bracketedPaste`), the editor handles the `paste` key.

### Terminal features

`queryCapabilities(f, { input, timeout, raw, env })` asks the terminal which features it has: it
sends DECRQM for modes 2026, 2027, 2004, 1004 and 1006, the kitty keyboard query, XTVERSION and
the primary device attributes last, and reads the answers until that last one came or `timeout`
(300 ms) passed. Call it once at start-up, before the line editor reads the input; what the user
typed meanwhile is returned. No terminal or no answer gives `false` everywhere.

```js
const caps = queryCapabilities(std.out);
// { syncOutput, graphemeClusters, bracketedPaste, focusEvents, sgrMouse, kittyKeyboard, kittyFlags,
//   truecolor (from COLORTERM), terminal ('kitty(0.35)'), answered, typeahead }
const restore = enableCapabilities(std.out, caps, { kitty: true });   // returns the undo function
const screen = new Screen(std.out, { sync: caps.syncOutput });        // each frame in one 2026 block
```

| Export | Description |
| --- | --- |
| `syncOutput(f, frame)`, `SYNC_BEGIN`, `SYNC_END` | one `ttyWrite` of the frame between begin and end of synchronized output (mode 2026) |
| `bracketedPasteEnable(f)` / `...Disable(f)` | mode 2004; `decodeKeys()` returns a paste as one key `{ name: 'paste', paste: text }` |
| `focusEventsEnable(f)` / `...Disable(f)` | mode 1004; keys `focusin` and `focusout` |
| `graphemeClustersEnable(f)` / `...Disable(f)` | mode 2027 |
| `kittyKeyboardEnable(f, flags = 1)` / `kittyKeyboardDisable(f)` | push and pop of the kitty keyboard flags; `decodeKeys()` reads `ESC [ code ; mods u` (with `event` press, repeat or release) |
| `parseCapabilityReplies(str)` | the answers found in `str`: `{ modes, kitty, version, da1, rest }` |
| `enableCapabilities(f, caps, options)` | switches on what `caps` has (`paste`, `graphemes` by default, `focus`, `kitty` on request), returns the function that switches it off |

`enableCapabilities` leaves the kitty keyboard off unless asked: with it on, Esc, Alt chords and
Ctrl chords no longer arrive as the legacy byte strings, so the code that reads the keys has to
go through `decodeKeys()`.

### Widths

`charWidth(ch)` (0 for controls and combining marks, 2 for wide characters, else 1),
`stringWidth(str)` (cells, CSI sequences left out), `stripAnsi(str)` (CSI sequences removed).

## Widgets

String builders for TUI layouts. Each returns a string or an array of lines and writes nothing,
so the result can go to `console.log`, a `Screen` (`screen.lines(row, col, lines)`) or another
widget. Widths are terminal cells (`stringWidth`), escape sequences in the input are kept.

```js
import { bigText, gradient, boxLines, inputBox, statusBar, BOX_ROUNDED } from 'terminal';

console.log(gradient(bigText('qjs', { half: true }), ['#6ec3f4', '#c084fc', '#f9a8d4']).join('\n'));
console.log(boxLines('Tips:\n1. /help', { title: 'qjs', chars: BOX_ROUNDED, border: { fg: '#c084fc' } }).join('\n'));
console.log(inputBox('', { width: 60, placeholder: 'Type your message' }).join('\n'));
console.log(statusBar(['~/src', 'no sandbox', 'model (100%)'], 60));
```

| Export | Result |
| --- | --- |
| `style(text, attrs)` | text with SGR attributes and a reset; `attrs`: `fg`, `bg` (`'#rrggbb'`, `'#rgb'`, `[r, g, b]`, a 0-255 palette index or a name like `red`), `bold`, `dim`, `italic`, `underline`, `inverse`, `strike` |
| `parseColor(c)` | `[r, g, b]` of `'#rrggbb'`, `'#rgb'` or an array, else `null` |
| `gradient(text, stops, bg)` | each column colored along the color `stops`, left to right; `text` is a string or lines, which share the columns (one gradient across a banner) |
| `truncate(str, width, ellipsis)` | cut to `width` cells with `…`, unchanged when it fits |
| `truncateLines(lines, width, ellipsis)` | `truncate()` of each element of `lines`, one native call on qjsm |
| `pad(str, width, align, fill)` | filled to `width` cells, `align` `left`, `right` or `center`, truncated when longer |
| `wrap(str, width)` | lines broken at spaces, long words split, a color running over a break closed and reopened |
| `rule(width, { char, label, style })` | `── label ─────` |
| `bigText(text, { half, fill, shadow })` | block letters, 5 lines; `half` packs them into 3 lines of `▀▄█`, `shadow` adds a `░` drop shadow |
| `boxLines(content, opts)` | bordered panel; `width`, `padding`, `title`, `titleAlign`, `footer`, `align`, `chars`, `border` (attrs); long lines are wrapped |
| `inputBox(value, opts)` | input line between two rules: `width`, `prompt`, `placeholder`, `cursor`, `rule`, `promptStyle`, `placeholderStyle` |
| `statusBar([left, center, right], width)` | one line, center kept centered while there is room |
| `keyValue(pairs, { width, keyStyle, gap })` | aligned `key  value` lines, null values left out |
| `table(rows, opts)` | bordered table; a row is cells or `{ section: title }`; `header`, `align`, `columnStyle` (per-column attrs over the padded cell, for highlighted columns), `headerStyle`, `sectionStyle`, `border`, `chars`, `maxWidth`; numbers align right |
| `progressBar(fraction, width, opts)` | bar in eighth-cell steps; `style`, `empty`, `emptyStyle` |
| `sparkline(values, { width, min, max })` | `▁▂▃▄▅▆▇█`, one cell per value |
| `SPINNERS` | frame sets: `dots`, `line`, `arc`, `circle`, `quadrant`, `bounce`, `pulse` |
| `scrollbar(height, total, offset)` | one cell per row, `┃` over the visible part |

### Scrolling

`class ScrollView(height, lines)` is a window of `height` lines onto a longer list. While
`follow` is on, new lines keep the end in view (a log); scrolling up turns it off, reaching the
bottom turns it on again.

```js
const view = new ScrollView(rows - 4);
view.push(...wrap(text, columns - 1));
if(view.handleKey(key)) screen.lines(1, 1, view.render(columns)).flush();
```

| Member | Description |
| --- | --- |
| `lines`, `offset`, `follow`, `height` | content, first visible line, end-following |
| `push(...lines)`, `setLines(lines)`, `resize(height)` | change content or height |
| `scrollTo(n)`, `scrollBy(n)`, `pageUp()`, `pageDown()`, `home()`, `end()` | move the window |
| `atBottom`, `maxOffset` | getters |
| `visible()` | the lines in the window |
| `render(width, { bar })` | exactly `height` lines of `width` cells, a scrollbar in the last column |
| `handleKey(key)` | scrolls for up, down, pageup, pagedown, home, end and the mouse wheel (3 lines); takes a `decodeKeys()` key or a `readKey()` result; returns whether it scrolled |

For scrolling the terminal itself use `setScrollRegion()` with `scrollUp()` / `scrollDown()` or
`insertLines()` / `deleteLines()`.

### Drawing without flicker

A status line or an input row that is redrawn on every key flickers when
the redraw starts with an erase (`CSI J`, `CSI 2K`) and then writes the
same text again. `Region` remembers what each of its rows shows and
writes only the rows that changed, each erased to its end after the new
text instead of before it:

```js
const status = new Region(rows - 1, 2, cols);
ttyWrite(f, status.update([left, right]));   // both rows the first time
ttyWrite(f, status.update([left, right]));   // '' - nothing to write
status.moveTo(newRows - 1, 2, newCols);       // after a resize: drawn again
status.invalidate();                          // after the screen was cleared
```

`layoutInput([prompt, cont], text, cursor, columns)` cuts an edited text
into screen rows (`prompt` before the first line, `cont` before each line
after a newline, wide characters counted as two cells) and gives the
cursor's row and column, so a line editor can draw its rows through a
`Region` and place the cursor without erasing what is below it:

```js
const { lines, row, col } = layoutInput(['> ', '. '], 'ab\ncd', 4, 80);
// lines ['> ab', '. cd'], row 1, col 3
```

## Export list

Generated by `utilities/doc-api.js` from the `terminal` dump of `utilities/api-surface.sh`: 162 members. Edit the text above this block, not the block.

**Functions:** `alternateScreen()`, `bigText()`, `boxLines()`, `centeredRect()`, `charWidth()`, `cursorBackward()`, `cursorDown()`, `cursorForward()`, `cursorHide()`, `cursorHome()`, `cursorHorizontalAbsolute()`, `cursorNextLine()`, `cursorOrigin()`, `cursorPosition()`, `cursorPreviousLine()`, `cursorQuery()`, `cursorRestore()`, `cursorSave()`, `cursorShow()`, `cursorUp()`, `decodeKeys()`, `deleteLines()`, `deviceReset()`, `devicecodeQuery()`, `devicestatusQuery()`, `disableRawMode()`, `enableRawMode()`, `eraseInDisplay()`, `eraseInLine()`, `gradient()`, `hslToRgb()`, `inputBox()`, `insertLines()`, `keyValue()`, `linewrapDisable()`, `linewrapEnable()`, `mouseDisable()`, `mouseEnable()`, `mousetrackingDisable()`, `mousetrackingEnable()`, `pad()`, `parseColor()`, `progressBar()`, `readKey()`, `resetScrollRegion()`, `reverseVideoOff()`, `reverseVideoOn()`, `rgbBackground()`, `rgbForeground()`, `rule()`, `scrollDown()`, `scrollUp()`, `scrollbar()`, `setAlternateScreen()`, `setNormalScreen()`, `setScreen()`, `setScrollRegion()`, `sgr()`, `sparkline()`, `statusBar()`, `stringWidth()`, `stripAnsi()`, `style()`, `tabClear()`, `tabSet()`, `table()`, `tabsClearall()`, `truncate()`, `truncateLines()`, `ttyGetWinSize()`, `ttyRead()`, `ttySetRaw()`, `ttyWrite()`, `windowSize()`, `wrap()`

**Values:** `EAGAIN = 11`

### `BOX_DOUBLE`

object

- **static values:** `b`, `bl`, `br`, `h`, `l`, `r`, `t`, `tl`, `tr`, `v`, `x`

### `BOX_HEAVY`

object

- **static values:** `b`, `bl`, `br`, `h`, `l`, `r`, `t`, `tl`, `tr`, `v`, `x`

### `BOX_ROUNDED`

object

- **static values:** `b`, `bl`, `br`, `h`, `l`, `r`, `t`, `tl`, `tr`, `v`, `x`

### `BOX_SINGLE`

object

- **static values:** `b`, `bl`, `br`, `h`, `l`, `r`, `t`, `tl`, `tr`, `v`, `x`

### `SPINNERS`

object

- **static values:** `arc`, `bounce`, `circle`, `dots`, `line`, `pulse`, `quadrant`

### `Screen`

class, constructible

- **methods:** `alternate()`, `bg()`, `box()`, `clear()`, `clearLine()`, `fg()`, `fillRect()`, `flush()`, `lines()`, `moveTo()`, `resetAttrs()`, `sgr()`, `write()`

### `ScrollView`

class, constructible

- **methods:** `end()`, `handleKey()`, `home()`, `pageDown()`, `pageUp()`, `push()`, `render()`, `resize()`, `scrollBy()`, `scrollTo()`, `setLines()`, `visible()`
- **properties:** `atBottom (get)`, `maxOffset (get)`

