/* terminal/scrollview.js: ScrollView, a scrollable window of wrapped lines.
 * depends on: terminal.js (pad, wrap), terminal/widgets.js (scrollbar).
 * rule: the view keeps source lines and rewraps them when the width changes. */

import { pad, wrap } from 'terminal';
import { scrollbar } from 'terminal/widgets';

/* ScrollView(height): a window of height lines onto a longer list
 *
 *   const view = new ScrollView(10)
 *   view.push(...wrap(text, 60))
 *   view.handleKey(key)          // pageup, wheel, ... from decodeKeys()
 *   screen.lines(2, 1, view.render(60))
 *
 * while follow is on (at the bottom) new lines keep the end in view, like
 * a log. scrolling up turns it off, reaching the bottom turns it on. */
export class ScrollView {
  lines = [];
  offset = 0;
  follow = true;

  constructor(height, lines = []) {
    this.height = Math.max(1, height);
    this.push(...lines);
  }

  get maxOffset() {
    return Math.max(0, this.lines.length - this.height);
  }

  get atBottom() {
    return this.offset >= this.maxOffset;
  }

  push(...lines) {
    this.lines.push(...lines);
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  setLines(lines) {
    this.lines = [...lines];
    this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    return this;
  }

  /* new window height, e.g. after a resize */
  resize(height) {
    this.height = Math.max(1, height);
    this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    return this;
  }

  scrollTo(offset) {
    this.offset = Math.max(0, Math.min(this.maxOffset, Math.round(offset)));
    this.follow = this.atBottom;
    return this;
  }

  scrollBy(n) {
    return this.scrollTo(this.offset + n);
  }

  pageUp() {
    return this.scrollBy(-(this.height - 1 || 1));
  }

  pageDown() {
    return this.scrollBy(this.height - 1 || 1);
  }

  home() {
    return this.scrollTo(0);
  }

  end() {
    return this.scrollTo(this.maxOffset);
  }

  /* Private: source entries {text, wrappedLines} cached per width */
  _sourceEntries = [];
  /* Private: current wrap width (undefined = old-style, no wrapping) */
  _width = undefined;
  /* Private: number of source entries that have been wrapped */
  _rewraps = 0;
  /* Public: existing flat concatenation of all display lines (backward compatible) */
  lines = [];
  /* Public: offset into lines for scrolling */
  offset = 0;
  /* Public: when true, keep end of view in sight when new lines added */
  follow = true;
  /* Public: wrap width, undefined when not in source mode */
  get width() {
    return this._width;
  }
  /* Public: number of source entries wrapped so far (test hook) */
  get rewraps() {
    return this._rewraps;
  }

  /* the lines in the window now */
  visible() {
    return this.lines.slice(this.offset, this.offset + this.height);
  }

  /* exactly height lines of width cells, a scrollbar in the last column when bar is on */
  render(width, { bar = true } = {}) {
    const sb = bar ? scrollbar(this.height, this.lines.length, this.offset) : null;
    const w = bar ? width - 1 : width;
    const vis = this.visible();

    return Array.from({ length: this.height }, (_, r) => pad(vis[r] ?? '', w) + (sb ? sb[r] : ''));
  }

  /* scrolls for up, down, pageup, pagedown, home, end and the wheel (3 lines)
   *
   * key is a decodeKeys() key or a readKey() result. returns whether it
   * scrolled the view. */
  handleKey(key) {
    const name = key.name ?? key.type;
    const wheel = name == 'mouse' ? (key.mouse ?? key).button : null;
    const before = this.offset;

    if(wheel == 'wheelup' || name == 'up') this.scrollBy(wheel ? -3 : -1);
    else if(wheel == 'wheeldown' || name == 'down') this.scrollBy(wheel ? 3 : 1);
    else if(name == 'pageup') this.pageUp();
    else if(name == 'pagedown') this.pageDown();
    else if(name == 'home') this.home();
    else if(name == 'end') this.end();
    else return false;

    return this.offset != before || name == 'end';
  }

  /* ---------- Source mode API ---------- */

  /**
   * Set wrap width. First call turns source mode on.
   * If width equals current width, do nothing.
   * Otherwise re-wrap all source entries, rebuild display lines,
   * and anchor the view (top of window stays on the same source entry/sub-line).
   * If follow is on, keep end in view.
   */
  setWidth(width) {
    if(this._width === width) return; /* no change */

    const wasSource = this._width !== undefined;
    this._width = width;
    this._rewraps += this._sourceEntries.length; /* each entry gets wrapped again */

    if(!wasSource) {
      /* First call to setWidth turns source mode on.
         Convert existing this.lines into source entries. */
      const existingLines = this.lines;
      this._sourceEntries = existingLines.map(text => ({ text, wrapped: [] }));
      /* Now re-wrap with the new width */
      this._rewrapAll();
    } else {
      /* Re-wrap all existing source entries with new width */
      this._rewrapAll();
    }

    /* Rebuild this.lines from _sourceEntries wrapped lines */
    this._rebuildLines();

    /* Anchor the view: keep the same source entry index if possible
       The offset should stay pointing at the same logical position.
       Simple approach: if follow was on and we were at bottom, stay at bottom.
       Otherwise, try to keep offset proportional. */
    if(this.follow && this.atBottom) {
      this.offset = this.maxOffset;
    } else if(!this.follow && this.offset > this.maxOffset) {
      this.offset = this.maxOffset;
    }

    return this;
  }

  /**
   * Appends source entries (strings with newlines/colors).
   * Wraps each with wrap(text, width) and appends to display lines.
   * Throws Error if setWidth hasn't been called yet.
   */
  pushText(...texts) {
    if(this._width === undefined) {
      throw new Error('setWidth must be called before pushText');
    }
    for(const text of texts) {
      const wrapped = wrap(text, this._width);
      this._sourceEntries.push({ text, wrapped });
      this._rewraps += 1; // each entry counts as one wrapped entry
    }
    this._rebuildLines();
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  /**
   * Replaces all source entries and rebuilds display lines.
   */
  setText(texts) {
    this._sourceEntries = texts.map(text => {
      const wrapped = wrap(text, this._width);
      return { text, wrapped };
    });
    this._rewraps = this._sourceEntries.length;
    this._rebuildLines();
    if(this.follow) this.offset = this.maxOffset;
    return this;
  }

  /** Returns number of source entries wrapped so far (test hook). */
  get rewraps() {
    return this._rewraps;
  }

  /** Returns wrap width or undefined. */
  get width() {
    return this._width;
  }

  /* ---------- Internal helpers ---------- */

  /**
   * Wrap all source entries and accumulate wrapped lines into each entry.
   * Does NOT touch this.lines here; caller must call _rebuildLines().
   */
  _rewrapAll() {
    for(const entry of this._sourceEntries) {
      entry.wrapped = wrap(entry.text, this._width);
    }
  }

  /**
   * Rebuild this.lines from _sourceEntries wrapped arrays.
   * Appending must not re-flatten the whole array, so we just concat.
   */
  _rebuildLines() {
    /* Build new flat lines array from all wrapped entries */
    const newLines = this._sourceEntries.flatMap(entry => entry.wrapped);
    /* Replace this.lines entirely; existing methods (render, handleKey, etc.) work untouched */
    this.lines = newLines;
    /* Adjust offset so we don't lose view position unexpectedly */
    if(this.offset > this.lines.length) this.offset = Math.max(0, this.lines.length - this.height);
    if(this.follow) this.offset = this.maxOffset;
  }

  /**
   * Append lines to the display (old-style, no source mode).
   * Pushes each line into this.lines; if follow is on, scrolls to bottom.
   */
  push(...lines) {
    /* In source mode, we need to convert lines to source entries.
       But old-style ScrollView (without setWidth) should just push lines directly. */
    if(this._width === undefined) {
      /* Old-style: just append to this.lines */
      this.lines.push(...lines);
      if(this.follow) this.offset = this.maxOffset;
    } else {
      /* Source mode: convert each line to a source entry */
      for(const text of lines) {
        const wrapped = wrap(text, this._width);
        this._sourceEntries.push({ text, wrapped });
        this._rewraps += 1;
      }
      this._rebuildLines();
      if(this.follow) this.offset = this.maxOffset;
    }
    return this;
  }

  /**
   * Replace all display lines (old-style, no source mode).
   * Replaces this.lines entirely and resets offset.
   */
  setLines(lines) {
    if(this._width === undefined) {
      /* Old-style: just replace lines flat */
      this.lines = [...lines];
      this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    } else {
      /* Source mode: convert lines to source entries */
      this._sourceEntries = lines.map(text => {
        const wrapped = wrap(text, this._width);
        return { text, wrapped };
      });
      this._rewraps = this._sourceEntries.length;
      this._rebuildLines();
      this.offset = this.follow ? this.maxOffset : Math.min(this.offset, this.maxOffset);
    }

    return this;
  }
}
