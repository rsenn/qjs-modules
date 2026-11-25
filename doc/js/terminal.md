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
| `\r` | `\x1b[13u` | `enter` |
| `\x7f` (Backspace) | `\x1b[127u` | `backspace` |
| `\x1bOQ` / `\x1b[12~` | — | `f2` |
| `\x1b[1;2Q` | `\x1b[24~` | `f2` (shift), `f12` (no shift) |
| `\x1b[5~` / `\x1b[6~` | — | `pageup` / `pagedown` |
| `\x1b\x1b` | — | `escape` (meta true) |

Legacy xterm sequences use `ESC [ ... ~` or `ESC O` prefixes, while kitty sends `ESC [ code ; mods u`. The decoder maps both to the same `{name, ctrl, meta, shift}` representation.