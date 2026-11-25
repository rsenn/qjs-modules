/* terminal/lineeditor.js: LineEditor, a line editing state machine over decoded keys.
 * depends on: terminal.js (layoutInput, prevCluster, nextCluster, stringWidth).
 * rule: it never reads or writes a tty, key() takes a key object and says what changed. */

import { layoutInput, nextCluster, prevCluster, stringWidth } from 'terminal';

/**
 * A stateful, single- and multi-line terminal text editor with Readline-style
 * keybindings, command history, reverse incremental search, kill ring, and tab
 * completion.
 *
 * Processes normalized key objects produced by {@link decodeKeys} via
 * {@link LineEditor#key} and computes wrapped cursor coordinates and
 * ghost text via {@link LineEditor#layout}.
 *
 * @example
 * const editor = new LineEditor({ history: ['git status', 'npm test'], maxHistory: 100 });
 * const action = editor.key(decodedKey);
 * if(action === 'submit') {
 *   const command = editor.accept();
 * }
 */
export class LineEditor {
  constructor({ history, maxHistory } = {}) {
    this.cmd = '';
    this.cursor = 0;
    this.killRing = '';
    this.complete = null; /* (text, pos) => { suggestion: string, range: [start, end] } | null */
    this.ghost = ''; /* current ghost text to display */
    this.searchMode = false;
    this.searchBuffer = '';
    this.searchIndex = -1;
    this.history = history !== undefined ? [...history] : [];
    this.historyPos = -1; /* -1 = a new entry, else the index of the entry shown (history.length = a new empty one) */
    this.maxHistory = maxHistory;
    this.multiline = false; /* when true, Enter inserts a newline */
    this.isIncomplete = null; /* (text) => boolean */
    this.onRemember = null; /* (entry) => void */
    this.historyFilter = null; /* (entry) => boolean */
  }

  /* one key from decodeKeys(): 'change', 'submit', 'cancel', 'eof' or null (not handled, nothing changed).
   *
   *   Ctrl-C, Escape        clear the line, 'cancel'      Ctrl-D   'eof' on an empty line, else delete right
   *   Return, Enter         'submit', a newline when multiline, or with Alt or Shift
   *   Ctrl-K, Ctrl-U        kill to the end / start of the line
   *   Ctrl-W, Ctrl-Backspace, Alt-Backspace   kill the word left     Ctrl-Delete, Alt-D   the word right
   *   the names ctrlc, ctrlD and ctrlJ of older callers are still understood
   */
  key(key) {
    const { name = '', ctrl = false, meta = false, shift = false } = key;
    const before = this.cmd + '\0' + this.cursor;
    const done = () => (this.cmd + '\0' + this.cursor != before ? 'change' : null);

    if(name == 'escape' || name == 'ctrlc' || (ctrl && name == 'c')) {
      if(this.searchMode) {
        /* exit search mode, restore previous cmd/cursor */
        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'change';
      }

      this.cmd = '';
      this.cursor = 0;
      this.historyPos = -1;

      return 'cancel';
    }

    /* ctrlR: enter reverse search mode */
    if(name == 'ctrlR' && !this.searchMode) {
      this.searchMode = true;
      this.searchBuffer = '';
      this.searchIndex = this.history.length - 1;

      return 'change';
    }

    if(name == 'tab') {
      const result = this.complete?.(this.cmd, this.cursor);
      if(result && result.suggestion !== null) {
        /* Insert the suggestion at the range */
        const [start, end] = result.range;
        /* If start is beyond or at the end of the text, just append the suggestion */
        if(start >= this.cmd.length) {
          this.cmd += result.suggestion;
          this.cursor = this.cmd.length;
        } else {
          /* effectiveEnd is the actual end position, clamped to text length */
          const effectiveEnd = end > this.cmd.length ? this.cmd.length : end;
          /* Insert suggestion, replacing text from start to effectiveEnd */
          this.cmd = this.cmd.substring(0, start) + result.suggestion + this.cmd.substring(effectiveEnd);
          this.cursor = start + result.suggestion.length;
        }
        /* Update ghost: if the suggestion is longer than the original range,
           the ghost contains the remaining suggestion text beyond what was in the range */
        const originalRangeLength = end - start;
        if(result.suggestion.length > originalRangeLength) {
          this.ghost = result.suggestion.substring(originalRangeLength);
        } else {
          this.ghost = '';
        }
      }
      return 'change';
    }

    if(this.searchMode) {
      if(name == 'escape' || name == 'ctrlC') {
        /* exit search mode, restore previous cmd/cursor */
        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'change';
      }

      if(name == 'enter') {
        /* accept current match, exit search mode */
        if(this.searchIndex >= 0 && this.searchIndex < this.history.length) {
          this.cmd = this.history[this.searchIndex];
          this.cursor = this.cmd.length;
        }

        this.searchMode = false;
        this.searchBuffer = '';
        this.searchIndex = -1;

        return 'submit';
      }

      if(name == 'backspace') {
        /* remove last char from searchBuffer, re-search */
        this.searchBuffer = this.searchBuffer.slice(0, -1);
        this._searchHistory(0); // <--- FIXED: 0 means "re-search current and older"
        return 'change';
      }

      if(name == 'up' || name == 'down') {
        /* navigate through matching history entries */
        this._searchHistory(name == 'up' ? -1 : 1);

        return 'change';
      }

      /* printable character: append to searchBuffer, re-search */
      if(name.length == 1 && /^[\x20-\x7e]$/.test(name)) {
        this.searchBuffer += name;
        this._searchHistory();

        return 'change';
      }
    }

    if(name == 'ctrlD' || (ctrl && name == 'd')) {
      if(this.cmd === '') {
        this.historyPos = -1;

        return 'eof';
      }

      this.deleteRight();

      return done();
    }

    if(name == 'paste') {
      this.insertText(String(key.paste ?? '').replace(/\r\n?/g, '\n'));

      return done();
    }

    if(name == 'return' || name == 'enter' || name == 'ctrlJ') {
      if(name == 'ctrlJ' || shift) {
        this.insertPrintable('\n');
        return 'change';
      }

      if(this.multiline || (this.isIncomplete && this.isIncomplete(this.cmd)) || meta) {
        this.insertText('\n');
        return 'change';
      }

      this.historyPos = -1;

      return 'submit';
    }

    if(ctrl) {
      switch (name) {
        case 'k':
          this.killToEnd();
          break;
        case 'u':
          this.killToStart();
          break;
        case 'w':
        case 'backspace':
          this.killWordLeft();
          break;
        case 'delete':
          this.killWordRight();
          break;
        case 'y':
          this.insertText(this.killRing);
          break;
        case 'a':
          this.cursor = this.lineStart();
          break;
        case 'e':
          this.cursor = this.lineEnd();
          break;
        case 'b':
          this.cursor = this.prevIndex(this.cursor);
          break;
        case 'f':
          this.cursor = this.nextIndex(this.cursor);
          break;
        case 'h':
          this.deleteLeft();
          break;
        case 'left':
          this.cursor = this.prevWordPos(this.cursor);
          break;
        case 'right':
          this.cursor = this.nextWordPos(this.cursor);
          break;
        case 'p':
          return this.arrow('up');
        case 'n':
          return this.arrow('down');
      }

      return done();
    }

    if(meta) {
      switch (name) {
        case 'b':
        case 'left':
          this.cursor = this.prevWordPos(this.cursor);
          break;
        case 'f':
        case 'right':
          this.cursor = this.nextWordPos(this.cursor);
          break;
        case 'd':
        case 'delete':
          this.killWordRight();
          break;
        case 'backspace':
          this.killWordLeft();
          break;
      }

      return done();
    }

    switch (name) {
      case 'space':
        this.insertText(' ');
        break;
      case 'backspace':
        this.deleteLeft();
        break;
      case 'delete':
        this.deleteRight();
        break;
      case 'left':
        this.cursor = this.prevIndex(this.cursor);
        break;
      case 'right':
        this.cursor = this.nextIndex(this.cursor);
        break;
      case 'home':
        this.cursor = this.lineStart();
        break;
      case 'end':
        this.cursor = this.lineEnd();
        break;
      case 'up':
      case 'down':
        return this.arrow(name);
      default:
        /* one code point is a character, longer names (f1, pageup, tab) are keys */
        if([...name].length == 1) this.insertText(name);
    }

    return done();
  }

  /* Up and Down: the line above or below in a text with newlines, else the history */
  arrow(name) {
    const start = this.lineStart();
    const col = this.cursor - start;

    if(name == 'up' ? start > 0 : this.lineEnd() < this.cmd.length) {
      const target = name == 'up' ? this.lineStart(start - 1) : this.lineEnd() + 1;

      this.cursor = Math.min(target + col, this.lineEnd(target));

      return 'change';
    }

    return this.navigateHistory(name == 'up' ? -1 : 1) ? 'change' : null;
  }

  /* start and end of the line of the text that holds pos: 'ab\ncd', 4 -> 3, 5 */
  lineStart(pos = this.cursor) {
    return pos > 0 ? this.cmd.lastIndexOf('\n', pos - 1) + 1 : 0;
  }

  lineEnd(pos = this.cursor) {
    const end = this.cmd.indexOf('\n', pos);

    return end < 0 ? this.cmd.length : end;
  }

  /* the index of the character before / after pos: a surrogate pair, a combining mark, a ZWJ emoji or a flag is one */
  prevIndex(pos) {
    return pos <= 0 ? 0 : prevCluster(this.cmd, pos);
  }

  nextIndex(pos) {
    return pos >= this.cmd.length ? this.cmd.length : nextCluster(this.cmd, pos);
  }

  insertText(text) {
    if(!text) return;

    this.cmd = this.cmd.substring(0, this.cursor) + text + this.cmd.substring(this.cursor);
    this.cursor += text.length;
  }

  insertPrintable(ch) {
    this.insertText(ch);
  }

  deleteLeft() {
    const at = this.prevIndex(this.cursor);

    this.cmd = this.cmd.substring(0, at) + this.cmd.substring(this.cursor);
    this.cursor = at;
  }

  deleteRight() {
    this.cmd = this.cmd.substring(0, this.cursor) + this.cmd.substring(this.nextIndex(this.cursor));
  }

  /* Ctrl-K: to the end of the line, at its end the newline */
  killToEnd() {
    const end = this.lineEnd();

    this.kill(this.cursor, end == this.cursor && end < this.cmd.length ? end + 1 : end);
  }

  /* Ctrl-U: to the start of the line */
  killToStart() {
    this.kill(this.lineStart(), this.cursor);
  }

  killWordLeft() {
    this.kill(this.prevWordPos(this.cursor), this.cursor);
  }

  killWordRight() {
    this.kill(this.cursor, this.nextWordPos(this.cursor));
  }

  /* takes cmd[from, to) out into killRing, the cursor goes to from */
  kill(from, to) {
    if(to <= from) return;

    this.killRing = this.cmd.substring(from, to);
    this.cmd = this.cmd.substring(0, from) + this.cmd.substring(to);
    this.cursor = from;
  }

  /* the start of the word before pos: spaces are skipped, then the word */
  prevWordPos(pos) {
    while(pos > 0 && /\s/.test(this.cmd[pos - 1])) pos--;
    while(pos > 0 && !/\s/.test(this.cmd[pos - 1])) pos--;

    return pos;
  }

  /* the end of the word after pos: spaces are skipped, then the word */
  nextWordPos(pos) {
    while(pos < this.cmd.length && /\s/.test(this.cmd[pos])) pos++;
    while(pos < this.cmd.length && !/\s/.test(this.cmd[pos])) pos++;

    return pos;
  }

  /* adds text to the history unless it is empty or the newest entry; maxHistory drops the oldest */
  remember(text) {
    if(!text || text === this.history[this.history.length - 1]) return;
    if(this.historyFilter && !this.historyFilter(text)) return;

    this.history.push(text);

    if(this.maxHistory !== undefined && this.history.length > this.maxHistory) this.history.shift();

    if(this.onRemember) this.onRemember(text);
  }

  loadHistory(entries) {
    this.history = [...entries];
    this.historyPos = -1;
  }

  saveHistory() {
    return [...this.history];
  }

  /* delta -1 older, +1 newer. Leaving a new entry with text keeps it as the newest history entry,
   * past the newest the line is a new empty one. true if the line changed; Down on a new entry does nothing.
   */
  navigateHistory(delta) {
    /* a new entry has nothing newer to go to */
    if(this.historyPos == -1 && delta > 0) return false;

    if(this.historyPos == -1) {
      if(this.cmd !== '') {
        this.remember(this.cmd);
        this.historyPos = this.history.length - 1;
      } else {
        this.historyPos = this.history.length;
      }
    }

    const pos = Math.max(0, Math.min(this.history.length, this.historyPos + delta));
    const cmd = pos == this.history.length ? '' : this.history[pos];
    const changed = pos != this.historyPos || cmd != this.cmd;

    this.historyPos = pos;
    this.cmd = cmd;
    this.cursor = cmd.length;

    return changed;
  }

  /* _searchHistory(direction): find history entries containing searchBuffer
   * direction: 0 = re-search (current then older), -1 = older, 1 = newer
   */
  _searchHistory(direction = 0) {
    const query = this.searchBuffer;
    let idx = this.searchIndex;

    if(direction === 0) {
      /* Re-search: check if current index still matches, else go older */
      if(idx >= 0 && idx < this.history.length && this.history[idx].includes(query)) {
        /* current still matches */
      } else {
        idx = idx >= 0 ? idx - 1 : this.history.length - 1;
        while(idx >= 0) {
          if(this.history[idx].includes(query)) break;
          idx--;
        }
      }
    } else if(direction === -1) {
      /* Older */
      idx = idx >= 0 ? idx - 1 : this.history.length - 1;
      while(idx >= 0) {
        if(this.history[idx].includes(query)) break;
        idx--;
      }
    } else if(direction === 1) {
      /* Newer */
      idx = idx + 1;
      while(idx < this.history.length) {
        if(this.history[idx].includes(query)) break;
        idx++;
      }
    }

    if(idx >= 0 && idx < this.history.length) {
      this.searchIndex = idx;
      this.cmd = this.history[idx];
      /* place cursor at the end of the matched substring, or end of line */
      const matchIdx = this.cmd.indexOf(query);
      this.cursor = matchIdx >= 0 ? matchIdx + query.length : this.cmd.length;
    } else {
      /* no match found */
      this.searchIndex = -1;
    }
  }

  set(cmd) {
    this.cmd = cmd !== undefined ? cmd : '';
    this.cursor = this.cmd.length;
    this.historyPos = -1;
    this.killRing = '';
  }

  accept() {
    const text = this.cmd;

    this.cmd = '';
    this.cursor = 0;
    this.historyPos = -1;
    this.killRing = '';
    this.remember(text);

    return text;
  }

  layout(prefixes, columns) {
    const { lines, row, col } = layoutInput(prefixes, this.cmd, this.cursor, columns);

    /* Calculate ghost lines based on this.ghost */
    const ghostLines = [];
    if(this.ghost) {
      const ghostLinesRaw = String(this.ghost).split('\n');

      for(const gLine of ghostLinesRaw) {
        /* Ensure ghost text starts at the correct cell position using stringWidth */
        const prefixWidth = stringWidth(lines[row] || '');
        ghostLines.push(gLine);
      }
    }

    return { lines, row, col, ghostLines };
  }
}
