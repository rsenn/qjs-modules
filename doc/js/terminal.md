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
| `lines` | The initial list of lines to display |
| `push(...texts)` | Appends source entries (strings with newlines/colors) to the display. Throws an error if `setWidth` hasn't been called yet. |
| `setWidth(width)` | Sets the wrap width. First call turns on source text mode. If `width` equals the current width, does nothing. Otherwise, re-wraps all source entries and rebuilds the display lines. If `follow` is on, keeps the end in view. |
| `setText(texts)` | Replaces all source entries and rebuilds the display lines. |
| `render(width)` | Returns the rendered lines as a string array, with the current viewport height. |
| `visible()` | Returns the visible lines as an array. |
| `handleKey(key)` | Processes a key event and returns `true` if it was handled. |
| `follow` | Whether the view follows the end of the list when new entries are added. |
| `atBottom` | Whether the view is at the bottom of the list. |
| `rewraps` | The number of source entries that have been wrapped so far (test hook). |
| `width` | The current wrap width, or `undefined` if not set. |

#### Wrapped text in a ScrollView

The `ScrollView` class supports an optional "source text" mode that caches wrapped lines and avoids re-wrapping on every resize. This mode is turned on when `setWidth()` is first called.

When in source text mode:

- `pushText()` appends source entries (strings with newlines/colors) and wraps each with `wrap(text, width)`
- `setText()` replaces all source entries and rebuilds the display lines
- `rewraps` tracks the number of source entries that have been wrapped so far
- `width` returns the current wrap width

This mode ensures that re-wrapping only happens when the width changes, improving performance for long logs or streams.

Note: The `Region` class does not wrap text.

Example:

```js
// Create a scroll view with 10 lines of height
const view = new ScrollView(10);

// Set the width to 0 cells
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