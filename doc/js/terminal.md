# terminal

`lib/terminal.js`: escape sequences, raw mode, window size, key decoding, cell widths, string widgets and a buffered `Screen`. On qjsm it uses `misc`, `os` and `textcode`; on node, bun and deno it uses `process` and `node:fs`, so the same program runs on all of them.

Every write is one `ttyWrite(f, string)` call. `f` is a std `FILE` on qjsm, and `process.stdout`, `process.stderr` or an fd number elsewhere. The widgets (`rule`, `table`, `boxLines`, ...) only build strings or arrays of lines and never write.

```js
import { Screen, enableRawMode, disableRawMode, windowSize, readKey, boxLines } from 'terminal';

enableRawMode(0);
const [columns, rows] = windowSize(1);
const screen = new Screen(process.stdout);
screen.alternate().clear().lines(1, 1, boxLines('hello', { width: 20 })).flush();
const key = readKey(0);
disableRawMode(0);
```

## Cursor, erase and scrolling

All take the output `f` first. A count `n` of 1 or less is left out of the sequence.

| Function | Sequence |
| --- | --- |
| `cursorUp(f, n)`, `cursorDown`, `cursorForward`, `cursorBackward` | move by `n` cells |
| `cursorNextLine(f, n)`, `cursorPreviousLine(f, n)` | move by `n` lines to column 1 |
| `cursorHorizontalAbsolute(f, n)` | to column `n` |
| `cursorPosition(f, row, column)`, `cursorOrigin(f)` | to row and column (1-based), to 1;1 |
| `cursorHome(f)` | carriage return |
| `cursorSave(f)`, `cursorRestore(f)` | save and restore the cursor |
| `cursorHide(f)`, `cursorShow(f)` | hide and show it |
| `cursorQuery(f)` | ask for the cursor position (`ESC [ 6 n`) |
| `eraseInDisplay(f, n = 0)`, `eraseInLine(f, n = 0)` | `n` 0 cursor to end, 1 start to cursor, 2 all |
| `insertLines(f, n)`, `deleteLines(f, n)` | blank lines at the cursor row, rows below move inside the scroll region |
| `scrollUp(f, n)`, `scrollDown(f, n)` | scroll the region |
| `setScrollRegion(f, top, bottom)`, `resetScrollRegion(f)` | scrolling stays within rows `top` to `bottom`, 1-based |
| `tabSet(f)`, `tabClear(f)`, `tabsClearall(f)` | tab stops |
| `devicecodeQuery(f)`, `devicestatusQuery(f)`, `deviceReset(f)` | primary device attributes, status report, full reset |
| `sgr(f, ...codes)` | `ESC [ codes m`, e.g. `sgr(f, 7)` |
| `reverseVideoOn(f)`, `reverseVideoOff(f)` | `sgr(f, 7)`, `sgr(f, 0)` |
| `rgbForeground(f, r, g, b)`, `rgbBackground(f, r, g, b)` | truecolor |

### Screens and modes

| Function | What it does |
| --- | --- |
| `setScreen(f, alternate = false)`, `setAlternateScreen(f)`, `setNormalScreen(f)` | switch between the alternate and the normal screen |
| `alternateScreen(f, fn)` | runs `fn` on the alternate screen and goes back after it, also when it throws. When `fn` returns a promise the switch back waits for it. |
| `linewrapEnable(f)`, `linewrapDisable(f)` | automatic line wrap |
| `mouseEnable(f, mode = 'click')`, `mouseDisable(f)` | SGR mouse reports that `decodeKeys()` reads. `click`: presses, releases, wheel. `drag`: also motion while a button is held. `move`: also motion without a button. |
| `mousetrackingEnable(f)`, `mousetrackingDisable(f)` | the older 1000;1006;1015 set |
| `bracketedPasteEnable(f)`, `bracketedPasteDisable(f)` | a paste arrives as one `paste` key |
| `focusEventsEnable(f)`, `focusEventsDisable(f)` | `focusin` and `focusout` keys |
| `graphemeClustersEnable(f)`, `graphemeClustersDisable(f)` | an emoji sequence or a base with its marks is one cell group |
| `kittyKeyboardEnable(f, flags = 1)`, `kittyKeyboardDisable(f)` | push and pop kitty keyboard flags. 1 reports Esc, Alt and Ctrl chords as `ESC [ code ; mods u`, 2 adds release and repeat, 8 reports every key. |
| `SYNC_BEGIN`, `SYNC_END`, `syncOutput(f, frame)` | synchronized output: `frame` is drawn as one frame without tearing, sent in one `ttyWrite`. A terminal without it ignores the sequences. |

`queryCapabilities()` tells which of these the terminal has.

## Raw mode, window size and input

| Function | Description |
| --- | --- |
| `enableRawMode(fd)`, `disableRawMode(fd)` | No echo and no line buffering on the input `fd`. Returns `true` when the output does VT itself. |
| `windowSize(f)` | `[columns, rows]` of the terminal behind a `FILE`, stream or fd, `[80, 24]` when it is none. |
| `ttyGetWinSize(fd)`, `ttyRead(fd, buffer, offset, length)`, `ttySetRaw(fd, restore)`, `ttyWrite(f, string)` | The four primitives the rest is built on. `ttyRead` returns `-EAGAIN` when nothing is there. |
| `EAGAIN` | The errno value that `ttyRead` negates. |
| `readKey(fd)` | One keypress from `fd` in raw mode, waits for it. |

`readKey(fd)` returns `{ type }` where `type` is `up`, `down`, `left`, `right`, `pageup`, `pagedown`, `space`, `enter`, `backspace`, `ctrlc`, `escape` (also any other sequence), `char` (with `char`), `mouse` (with `button`, `action`, `x`, `y`, 1-based, and `ctrl`, `meta`, `shift`, see `mouseEnable()`) or `eof`.

### Capabilities

```js
const caps = queryCapabilities(process.stdout);
const restore = enableCapabilities(process.stdout, caps, { kitty: true });
const screen = new Screen(process.stdout, { sync: caps.syncOutput });
// ... at exit
restore();
```

`queryCapabilities(f, { input = 0, timeout = 300, raw = true, env })` sends the questions, then reads the answers from the input until the last one came or `timeout` ms passed. Call it once at start-up, before anything else reads the input. It returns `{ syncOutput, graphemeClusters, bracketedPaste, focusEvents, sgrMouse, kittyKeyboard, kittyFlags, truecolor, terminal, answered, typeahead }`. A terminal that answers nothing, or no terminal, gives `false` everywhere. `truecolor` comes from `COLORTERM` and `TERM`. What the user typed meanwhile is in `typeahead`.

`parseCapabilityReplies(str)` is the parser behind it: `{ modes, kitty, version, da1, rest }` with `modes` mapping a mode number to 0 unknown, 1 set, 2 reset, 3 always set, 4 always reset.

`enableCapabilities(f, caps, { paste = true, graphemes = true, focus = false, kitty = false })` switches on what `caps` says the terminal has and returns a function that switches it off. `focus` and `kitty` are off by default, since the code reading the keys has to understand what they change. Synchronized output is per frame: `new Screen(f, { sync: caps.syncOutput })`.

## Colors and text

| Function | Description |
| --- | --- |
| `style(text, attrs)` | `text` with SGR attributes and a reset after it. `attrs`: `fg`, `bg` (`'#rrggbb'`, `'#rgb'`, `[r, g, b]`, a 0-255 palette index or a name), `bold`, `dim`, `italic`, `underline`, `inverse`, `strike`. `''` stays `''`. |
| `parseColor(c)` | `[r, g, b]` of `'#rrggbb'`, `'#rgb'` or `[r, g, b]`, `null` for anything else. |
| `hslToRgb(h, s, l)` | `[r, g, b]` (0-255) for `h` in degrees, `s` and `l` in 0-1. Walking `h` by 137.508 gives well spread hues. |
| `gradient(text, stops, bg = false)` | Each column colored along `stops`, left to right. `text` is a string or lines, lines share the columns. Spaces stay uncolored. |
| `stringWidth(str)`, `charWidth(ch)` | Terminal cells of a string or one character: controls and combining marks 0, East Asian wide and emoji 2, the rest 1, escape sequences 0. |
| `prevCluster(str, pos)`, `nextCluster(str, pos)` | The UTF-16 index of the character boundary before / after `pos`: a surrogate pair, a letter with its combining marks, a ZWJ emoji sequence, a flag, a Hangul syllable and CR LF are one character (a compact UAX 29). `nextCluster('e\u0301x', 0)` is 2. Native in `textcode` on qjsm, a JS twin elsewhere. |
| `stripAnsi(str)` | `str` without CSI, OSC, DCS/SOS/PM/APC and two byte escape sequences. |
| `truncate(str, width, ellipsis = '…')` | Cut to `width` cells, escapes kept and a reset appended when it had any. |
| `truncateLines(lines, width, ellipsis = '…')` | `truncate()` of each element, one native call. |
| `wrap(str, width)` | Lines of at most `width` cells, broken at spaces. Newlines are kept, long words split, a color that runs over a break is closed and reopened. |
| `pad(str, width, align = 'left', fill = ' ')` | `str` filled to `width` cells (`left`, `right`, `center`), truncated when longer. |

The width, strip, truncate and wrap functions are native in `textcode` where it has them and pure JS elsewhere, with the same results (see [textcode](../native/textcode.md)).

## Widgets

Each returns a string or an array of lines. Draw them with `Screen.lines()`.

| Function | Description |
| --- | --- |
| `rule(width, { char = '─', label = '', style })` | A horizontal line with a label after the first two cells: `rule(20, { label: 'Thinking' })` is `'── Thinking ────────'`. |
| `boxLines(content, { width, padding = 1, title, titleAlign = 'left', footer, align = 'left', chars = BOX_SINGLE, border })` | `content` (string or lines, longer ones wrapped) inside a border. `width` is the outer width. |
| `inputBox(value, { width = 80, prompt = '> ', placeholder = '', cursor = false, rule, promptStyle, placeholderStyle })` | A one-line input field between two rules. The text keeps its end visible. |
| `statusBar([left, center, right], width)` | Left, center and right text in one line. The center stays centered while there is room, else left, then center, are truncated. |
| `keyValue(pairs, { width, keyStyle, gap = 2 })` | Aligned key and value lines from an object or `[key, value]` entries, `null` values left out. Multi-line values are indented under the first line. |
| `table(rows, { header, align, columnStyle, headerStyle, sectionStyle, border, chars, maxWidth })` | A bordered table as lines. A row is an array of cells or `{ section: title }`. `align` is per column (`left`, `right`, `center`, numbers go right), `columnStyle` styles the whole padded cell, `maxWidth` truncates the widest columns. |
| `progressBar(fraction, width, { style, empty = ' ', emptyStyle })` | A bar in eighth-cell steps: `progressBar(0.5, 10)` is `'█████     '`. |
| `sparkline(values, { width, min, max })` | One cell per value, the last `width` ones: `sparkline([1, 5, 3, 8])` is `'▁▅▃█'`. `null` is a space. |
| `scrollbar(height, total, offset)` | One cell per row, `┃` for the visible part, all blank when everything fits. |
| `bigText(text, { half = false, fill = '█', shadow = false })` | Block letters as lines, 5 rows, or 3 with `half`. Letters are upper-cased, unknown ones become `?`. |
| `SPINNERS` | Frame sets: `SPINNERS.dots[i % SPINNERS.dots.length]`. |
| `centeredRect(termCols, termRows, width, height)` | `{ row, col, width, height }` of a centered panel, clamped to the screen, 1-based. |
| `BOX_SINGLE`, `BOX_DOUBLE`, `BOX_ROUNDED`, `BOX_HEAVY` | Box sets: corners `tl tr bl br`, lines `h v`, tees `t b l r` and the cross `x`. |

`table()` is the bordered one. `fitTable()` (below) is the plain aligned one that shrinks to a width.

## Screen

`new Screen(f, { sync = false })` is a string backbuffer: the methods append to it and `flush()` sends the whole frame in one `ttyWrite`, which avoids the flicker of drawing cell by cell. Later draws cover earlier ones. With `sync`, `flush()` wraps the frame in synchronized output. All methods but `flush()` return the screen.

| Method | Description |
| --- | --- |
| `write(s)` | Raw text, nothing escaped. |
| `moveTo(row, column)` | Cursor position, 1-based. |
| `clear(n = 2)`, `clearLine(n = 2)` | Erase in display or line, `n` as in `eraseInDisplay()`. |
| `fg(r, g, b)`, `bg(r, g, b)`, `sgr(...codes)`, `resetAttrs()` | Colors and attributes. |
| `fillRect(row, col, width, height, ch = ' ')` | Fills a rectangle, covering what was there. |
| `box(row, col, width, height, { title, chars = BOX_SINGLE })` | A bordered, filled panel, the title centered into the top border. Content goes in rows `row+1` to `row+height-2`. |
| `alternate(on = true)` | Alternate screen on or off. |
| `lines(row, col, lines)` | Lines one below the other from `row`, `col`. |
| `flush()` | Sends the buffer and empties it. |

### Region

`new Region(top, height, width = Infinity)` is rows of the screen that remember what they show. `update(lines)` returns the escape sequences that rewrite only the rows whose text changed, each erased to its end, so nothing else is touched and nothing flickers. It returns `''` when nothing changed.

```js
const status = new Region(23, 2, 80);
ttyWrite(f, status.update(['model x', 'tokens 12']));   // both rows
ttyWrite(f, status.update(['model x', 'tokens 13']));   // the second row only
```

`moveTo(top, height, width)` moves it, `invalidate()` forgets what it shows (after the screen was cleared or the region moved by other means). Lines must fit the width.

### layoutInput

`layoutInput([first, cont = ''], text, cursor, columns)` returns `{ lines, row, col }`: the rows an edited text takes and where the cursor is in them. `first` starts the text, `cont` each line after a newline in it. Rows are cut at `columns` cells, `cursor` is an index into `text`, and a cursor after a full row sits at the start of the next one.

```js
layoutInput(['> ', '. '], 'ab\ncd', 4, 80);   // { lines: ['> ab', '. cd'], row: 1, col: 3 }
```

## LineEditor

`new LineEditor({ history, maxHistory })` is the state of a line being edited: `cmd` (the text), `cursor`, `killRing`, `history`, `historyPos` and `multiline`. It draws nothing.

`key(key)` takes a key object from `decodeKeys()` and returns `'change'`, `'submit'`, `'cancel'`, `'eof'` or `null` (not handled, or nothing changed). `accept()` returns the text, clears the line and adds it to the history. `layout(prefixes, columns)` is `layoutInput()` of the current text. `set(cmd)` replaces the text.

| Keys | Action |
| --- | --- |
| printable keys, `space` | insert at the cursor; one code point is one key |
| `left`, `right`, `home`, `end`, Ctrl-B, Ctrl-F, Ctrl-A, Ctrl-E | move the cursor, Home, End, Ctrl-A and Ctrl-E on the line of a text with newlines |
| Ctrl-`left`, Ctrl-`right`, Alt-`left`, Alt-`right`, Alt-B, Alt-F | move by word: to the start of the word before, to the end of the word after |
| `backspace`, Ctrl-H, `delete` | delete left and right, a whole character: a letter with its accents, a ZWJ emoji, a flag |
| Ctrl-K, Ctrl-U | kill to the end, to the start of the line (into `killRing`, the cursor stays at the cut) |
| Ctrl-W, Ctrl-`backspace`, Alt-`backspace` | kill the word left; Ctrl-`delete`, Alt-D kill the word right |
| Ctrl-Y | yank the kill ring |
| `up`, `down`, Ctrl-P, Ctrl-N | the line above or below in a text with newlines, else the history |
| `return`, `enter` | `'submit'`, or a newline when `multiline` is set |
| Alt-Enter, Shift-Enter, `ctrlJ` | insert a newline |
| a bracketed paste | inserted, CRLF and CR as LF |
| Ctrl-C, `escape` | clear the line, `'cancel'` |
| Ctrl-D | `'eof'` on an empty line, else delete right |

`decodeKeys()` reports Ctrl chords as the letter with `ctrl` set (Ctrl-C is `c` with `ctrl`), the Return key as `return` and a line feed as `enter`; the editor tests exactly those. The names `ctrlc`, `ctrlD` and `ctrlJ` of older callers still work. A Ctrl chord that is not in the table does nothing, it does not insert a letter.

History: `historyPos` is -1 for a new entry, else the index of the entry shown, `history.length` for a new empty one. Up from a new entry with text keeps the text as the newest history entry first, then shows the older one. Down past the newest entry shows an empty line and adds nothing; Down on a new entry does nothing. `accept()` adds the text unless it is empty or equal to the newest entry, `maxHistory` drops the oldest.

The cursor steps over `prevCluster()` / `nextCluster()` boundaries (see Colors and text), so a surrogate pair, a letter with combining marks, a ZWJ emoji sequence and a flag are each one step.

`key()` returns `null` when the key changed nothing (Left at the start, Delete at the end), so a caller can skip the redraw.

### ScrollView

`class ScrollView(height, lines)` is a window of `height` lines onto a longer list. While `follow` is on, new lines keep the end in view (a log); scrolling up turns it off, reaching the bottom turns it on again.

```js
const view = new ScrollView(rows - 4);
view.push(...wrap(text, columns - 1));
if(view.handleKey(key)) screen.lines(1, 1, view.render(columns)).flush();
```

| Member | Description |
| --- | --- |
| `height` | The height of the viewport in lines |
| `lines` | The current flat display lines (backward compatible) |
| `push(...texts)` | Appends source entries (strings with newlines/colors) to the display. In source mode (after `setWidth`), each entry is wrapped and cached. |
| `setWidth(width)` | Sets the wrap width. First call turns on **source text mode**. If `width` equals the current width, does nothing. Otherwise, re-wraps all source entries and rebuilds the display lines. If `follow` is on, keeps the end in view. |
| `setText(texts)` | Replaces all source entries and rebuilds the display lines. |
| `render(width)` | Returns the rendered lines as a string array, with the current viewport height. |
| `visible()` | Returns the visible lines as an array. |
| `handleKey(key)` | Processes a key event and returns `true` if it was handled. |
| `follow` | Whether the view follows the end of the list when new entries are added. |
| `atBottom` | Whether the view is at the bottom of the list. |
| `rewraps` | The number of source entries that have been wrapped so far (test hook). |
| `width` | The current wrap width, or `undefined` if not in source mode. |
| `maxOffset` | Maximum offset (= `Math.max(0, this.lines.length - this.height)`). |
| `setLines(lines)` | Replaces all display lines (old-style, no source mode). |
| `resize(height)` | Updates the viewport height. |
| `scrollTo(offset)` | Scrolls to a given offset; resets `follow` when offset is extreme. |
| `scrollBy(n)` | Scrolls by `n` lines. |
| `pageUp()` | Scrolls up by `height - 1` lines. |
| `pageDown()` | Scrolls down by `height - 1` lines. |
| `home()` | Scrolls to the top (offset 0). |
| `end()` | Scrolls to the bottom. |

#### Wrapped text in a ScrollView

The `ScrollView` class supports an optional "source text" mode that caches wrapped lines and avoids re-wrapping on every resize. This mode is turned on when `setWidth()` is first called.

When in source text mode:

- `pushText(...texts)` appends source entries (strings with newlines/colors) and wraps each with `wrap(text, width)`. Throws an error if `setWidth` hasn't been called yet.
- `setText(texts)` replaces all source entries and rebuilds the display lines.
- `rewraps` tracks the number of source entries that have been wrapped so far.
- `width` returns the current wrap width.

This mode ensures that re-wrapping only happens when the width changes, improving performance for long logs or streams where the text content changes frequently.

Note: The `Region` class does not wrap text.

Example:

```js
// Create a scroll view with 10 lines of height
const view = new ScrollView(10);

// Set the width to 20 cells
view.setWidth(20);

// Push text entries
view.pushText('This is a long paragraph that will wrap at width twenty cells', 'Short entry');
view.pushText('Another entry');

// The rewraps property tracks how many entries have been wrapped
console.log('Rewraps:', view.rewraps); // Output: 3

// Change the width to 10 cells
view.setWidth(10);

// The rewraps property now tracks how many entries have been wrapped at the new width
console.log('Rewraps:', view.rewraps); // Output: 6

// The width property returns the current width
console.log('Width:', view.width); // Output: 10
```

The source text mode improves performance by avoiding re-wrapping on every resize, making it ideal for long logs or streams where the text content changes frequently.

### Fitting a table

`fitTable(width, make, flex, drop)` returns a table as text that is at most `width` cells wide. It is native in `textcode` where that has it, pure JS elsewhere, with the same result.

```js
import { fitTable } from 'terminal';

const make = bar => [
  ['Model', 'Rate', 'Says'],
  ...models.map(m => [m.name, '#'.repeat(bar) + m.rate, m.says]),
];

console.log(fitTable(process.stdout.columns, make, [2], [2]));
```

- `make(bar)` builds the rows, header first. It is called with `bar` = 6, then 3, then 0 until the table fits: build the meters of the table (a bar column) `bar` cells wide, and leave them out at 0. Without a `width` (0) the first try (6) is kept.
- `flex` are the column indices that may shrink (one number is fine, default `0`). A try with a meter is skipped when the first flex column would lose below 24 cells. The widest flex column gives up one cell at a time until the table fits or every flex column is down to its header width (at least 6); cells are cut with an ellipsis and keep a reset after a color.
- `drop` are column indices removed one by one, in the order given, when shrinking is not enough, each time starting again from `bar` 6. `make` still runs once for each `bar`: the rows it returned are kept and read again, never changed, so a dropped column costs no new `make` call. Indices of `flex` and `drop` count the columns `make` returns, dropped ones included.
- A column below the header whose cells are made only of `0-9 . , k M G % -` is right aligned, the others are left aligned. Columns are two spaces apart and trailing spaces are cut.

Widths are `stringWidth` cells, so a CJK character counts 2 and escape sequences count 0; cutting is `truncate()`, which is cell based too. The ollama-repl `usage-stats.js` this comes from counts code points and cuts a colored cell even when no escape sequence precedes the cut, so a table with wide characters or an escape after the cut can differ from it.

### Key names

The `decodeKeys()` function normalises both legacy xterm/ECMA-48 sequences and kitty keyboard sequences to produce identical key objects with `name`, `ctrl`, `meta`, and `shift` flags. The following matrix shows the equivalence:

| Legacy sequence | Kitty sequence | Key name |
|---|---|---|
| `\x1b` (final=true) | `\x1b[27u` | `escape` |
| `\x1b[Z` | `\x1b[9;2u` | `shift tab` |
| `\x1b\t` | `\x1b[9;3u` | `alt tab` |
| `\x1b1`–`\x1b4` | `\x1b[49;3u`–`\x1b[52;3u` | `alt 1`–`alt 4` |
| `\x1b0` | `\x1b[48;3u` | `alt 0` |
| `\x1b\\` | `\x1b[92;3u` | `alt backslash` |
| `\x1ba` | `\x1b[97;3u` | `alt letter` |
| `\x01` (Ctrl-A) | `\x1b[97;5u` | `ctrl letter` |
| `\r` | `\x1b[13u` | `return` |
| `\x7f` (Backspace) | `\x1b[127u` | `backspace` |
| `\x1bOQ` / `\x1b[12~` | — | `f2` |
| `\x1b[1;2Q` | `\x1b[24~` | `f2` (shift), `f12` (no shift) |
| `\x1b[5~` / `\x1b[6~` | — | `pageup` / `pagedown` |
| `\x1b\x1b` | — | `escape` (meta true) |

Legacy xterm sequences use `ESC [ ... ~` or `ESC O` prefixes, while kitty sends `ESC [ code ; mods u`. The decoder maps both to the same `{name, ctrl, meta, shift}` representation.

## Export list

Generated by `utilities/doc-api.js` from the `terminal` dump of `utilities/api-surface.sh`: 217 members. Edit the text above this block, not the block.

**Functions:** `alternateScreen()`, `bigText()`, `boxLines()`, `bracketedPasteDisable()`, `bracketedPasteEnable()`, `centeredRect()`, `charWidth()`, `cursorBackward()`, `cursorDown()`, `cursorForward()`, `cursorHide()`, `cursorHome()`, `cursorHorizontalAbsolute()`, `cursorNextLine()`, `cursorOrigin()`, `cursorPosition()`, `cursorPreviousLine()`, `cursorQuery()`, `cursorRestore()`, `cursorSave()`, `cursorShow()`, `cursorUp()`, `decodeKeys()`, `deleteLines()`, `deviceReset()`, `devicecodeQuery()`, `devicestatusQuery()`, `disableRawMode()`, `enableCapabilities()`, `enableRawMode()`, `eraseInDisplay()`, `eraseInLine()`, `fitTable()`, `focusEventsDisable()`, `focusEventsEnable()`, `gradient()`, `graphemeClustersDisable()`, `graphemeClustersEnable()`, `hslToRgb()`, `inputBox()`, `insertLines()`, `keyValue()`, `kittyKeyboardDisable()`, `kittyKeyboardEnable()`, `layoutInput()`, `linewrapDisable()`, `linewrapEnable()`, `mouseDisable()`, `mouseEnable()`, `mousetrackingDisable()`, `mousetrackingEnable()`, `nextCluster()`, `pad()`, `parseCapabilityReplies()`, `parseColor()`, `prevCluster()`, `progressBar()`, `queryCapabilities()`, `readKey()`, `resetScrollRegion()`, `reverseVideoOff()`, `reverseVideoOn()`, `rgbBackground()`, `rgbForeground()`, `rule()`, `scrollDown()`, `scrollUp()`, `scrollbar()`, `setAlternateScreen()`, `setNormalScreen()`, `setScreen()`, `setScrollRegion()`, `sgr()`, `sparkline()`, `statusBar()`, `stringWidth()`, `stripAnsi()`, `style()`, `syncOutput()`, `tabClear()`, `tabSet()`, `table()`, `tabsClearall()`, `truncate()`, `truncateLines()`, `ttyGetWinSize()`, `ttyRead()`, `ttySetRaw()`, `ttyWrite()`, `windowSize()`, `wrap()`

**Values:** `EAGAIN = 11`, `SYNC_BEGIN`, `SYNC_END`

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

### `LineEditor`

class, constructible

- **methods:** `accept()`, `arrow()`, `deleteLeft()`, `deleteRight()`, `insertPrintable()`, `insertText()`, `key()`, `kill()`, `killToEnd()`, `killToStart()`, `killWordLeft()`, `killWordRight()`, `layout()`, `lineEnd()`, `lineStart()`, `navigateHistory()`, `nextIndex()`, `nextWordPos()`, `prevIndex()`, `prevWordPos()`, `remember()`, `set()`

### `Region`

class, constructible

- **methods:** `invalidate()`, `moveTo()`, `update()`

### `SPINNERS`

object

- **static values:** `arc`, `bounce`, `circle`, `dots`, `line`, `pulse`, `quadrant`

### `Screen`

class, constructible

- **methods:** `alternate()`, `bg()`, `box()`, `clear()`, `clearLine()`, `fg()`, `fillRect()`, `flush()`, `lines()`, `moveTo()`, `resetAttrs()`, `sgr()`, `write()`

### `ScrollView`

class, constructible

- **methods:** `_rebuildLines()`, `_rewrapAll()`, `end()`, `handleKey()`, `home()`, `pageDown()`, `pageUp()`, `push()`, `pushText()`, `render()`, `resize()`, `scrollBy()`, `scrollTo()`, `setLines()`, `setText()`, `setWidth()`, `visible()`
- **properties:** `atBottom (get)`, `maxOffset (get)`, `rewraps (get)`, `width (get)`

