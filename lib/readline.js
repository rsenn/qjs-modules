/* readline.js: Node's `readline` (also Bun's, Deno's): line-by-line input and cursor helpers.
 * depends on: events (EventEmitter), terminal (tty reads, writes, keys, widths), textcode.
 * rule: no Node streams; `input` is a ReadableStream, an async iterable of string or bytes,
 *       a 'data'/'end' emitter, or a FILE (`process.stdin`); `terminal: true` edits the line.
 *
 * ```js
 * import * as readline from 'readline';
 *
 * const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
 *
 * for await (const line of rl) console.log(line);                  // lines of stdin
 * rl.question('name? ', name => { ... });                          // prompt, one answer
 * ```
 *
 * | export                                      | notes                                       |
 * | ------------------------------------------- | ------------------------------------------- |
 * | createInterface(options) / Interface        | 'line', 'close', 'pause', 'resume'; question(), prompt(), write(), close() |
 * | promises                                    | `readline/promises`: question() returns a promise |
 * | cursorTo, moveCursor, clearLine, clearScreenDown | write an ANSI sequence to `stream`   |
 *
 * lines end at `\n`, `\r\n` or `\r`; a last line without one is emitted when the input ends.
 * `terminal: true` (default: `output` is a tty) turns the input into keystrokes: cursor keys,
 * kill/yank, word moves, history, tab completion; a tty FILE input is put in raw mode.
 */
import { EventEmitter } from 'node/events';
import { isatty, setReadHandler } from 'os';
import { EAGAIN, charWidth, decodeKeys, stringWidth, stripAnsi, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } from 'terminal';
import { clearTimeout, setTimeout } from 'timers';
import { TextDecoder } from 'textcode';

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });
const useAfterClose = () => nodeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed');
const abortError = signal => Object.assign(new Error('The operation was aborted', { cause: signal.reason }), { name: 'AbortError', code: 'ABORT_ERR' });

const kWriter = Symbol('readline.writer');

/* write `s` to a Node writable, a FILE (through ttyWrite) or a WHATWG WritableStream; true when written */
function put(out, s, cb) {
  if(!out) return true;

  if(typeof out.fileno == 'function' && typeof out.puts == 'function') ttyWrite(out, s);
  else if(typeof out.puts == 'function') out.puts(s);
  else if(typeof out.write == 'function') return out.write(s, cb);
  else if(typeof out.getWriter == 'function') (out[kWriter] ??= out.getWriter()).write(s);
  else return true;

  if(typeof cb == 'function') Promise.resolve().then(cb);

  return true;
}

/* a pull function (async, `{ value, done }`) and a cancel function for ReadableStream and async iterables */
function pullSource(input) {
  if(typeof input.getReader == 'function') {
    const reader = input.getReader();

    return { next: () => reader.read(), cancel: () => reader.cancel().catch(() => {}) };
  }

  const it = input[Symbol.asyncIterator]();

  return { next: () => it.next(), cancel: () => it.return?.() };
}

const bytesOf = chunk => (chunk instanceof ArrayBuffer ? new Uint8Array(chunk) : new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength));

/* emitKeypressEvents(stream): make `stream` emit 'keypress' (char, key) for what it receives as 'data' */
export function emitKeypressEvents(stream, iface) {
  if(stream[kKeypress]) return;

  stream[kKeypress] = true;

  let rest = '';
  const decoder = new TextDecoder();
  const onData = chunk => {
    const { keys, rest: more } = decodeKeys(rest + (typeof chunk == 'string' ? chunk : decoder.decode(bytesOf(chunk), { stream: true })), false);

    rest = more;

    for(const { s, key } of keys) stream.emit('keypress', s, key);
  };

  if(stream.listenerCount?.('keypress') > 0) stream.on('data', onData);
  else
    stream.on('newListener', function onNewListener(event) {
      if(event == 'keypress') {
        stream.on('data', onData);
        stream.removeListener('newListener', onNewListener);
      }
    });
}

const kKeypress = Symbol('readline.keypress');

const charLengthLeft = (str, i) => (i > 1 && str.charCodeAt(i - 1) >= 0xdc00 && str.charCodeAt(i - 1) <= 0xdfff && str.charCodeAt(i - 2) >= 0xd800 && str.charCodeAt(i - 2) <= 0xdbff ? 2 : 1);
const charLengthAt = (str, i) => (str.codePointAt(i) > 0xffff ? 2 : 1);

const commonPrefix = list => list.reduce((p, c) => { let i = 0; while(i < p.length && i < c.length && p[i] == c[i]) i++; return p.slice(0, i); });

const isTTY = stream => !!stream && (stream.isTTY ?? (typeof stream.fileno == 'function' && isatty(stream.fileno())));

export class Interface extends EventEmitter {
  #input;
  #output;
  #prompt;
  #paused = false;
  #closed = false;
  #buffer = '';
  #sawReturn = false;
  #decoder = new TextDecoder();
  #question = null;
  #wake = null;
  #stop = null;
  #completer;
  #crlfDelay;
  #tabSize;
  #historySize;
  #removeDuplicates;
  #escapeTimeout;
  #historyIndex = -1;
  #search = null;
  #killed = '';
  #prevRows = 0;
  #prevKey = null;
  #sawReturnAt = null;
  #keyRest = '';
  #keyTimer = null;
  #rawFd = -1;

  constructor(input, output, completer, terminal) {
    super();

    const options = input && typeof input == 'object' && 'input' in input ? input : { input, output, completer, terminal };
    const src = options.input;

    if(!src || typeof src != 'object' || !(typeof src.getReader == 'function' || typeof src[Symbol.asyncIterator] == 'function' || typeof src.fileno == 'function' || typeof src.on == 'function'))
      throw new TypeError('The "input" argument must be a ReadableStream, async iterable, FILE or emitter of data');

    this.#input = src;
    this.#output = options.output;
    this.#prompt = options.prompt ?? '> ';
    this.#completer = options.completer;
    this.#crlfDelay = options.crlfDelay ?? 100;
    this.#tabSize = options.tabSize ?? 8;
    this.#historySize = options.historySize ?? 30;
    this.#removeDuplicates = !!options.removeHistoryDuplicates;
    this.#escapeTimeout = options.escapeCodeTimeout ?? 500;
    this.terminal = options.terminal ?? isTTY(options.output);
    this.line = '';
    this.cursor = 0;
    this.history = this.terminal ? [...(options.history ?? [])] : [];

    if(this.terminal) options.output?.on?.('resize', () => this.#refreshLine());

    if(options.signal) {
      if(options.signal.aborted) Promise.resolve().then(() => this.close());
      else options.signal.addEventListener('abort', () => this.close(), { once: true });
    }

    Promise.resolve().then(() => this.#start());
  }

  get input() {
    return this.#input;
  }

  get output() {
    return this.#output;
  }

  /* the width of the output in cells; Infinity when unknown */
  get columns() {
    const out = this.#output;

    if(out?.columns) return out.columns;

    if(typeof out?.fileno == 'function') return ttyGetWinSize(out.fileno())?.[0] || Infinity;

    return Infinity;
  }

  /* Node leaves `closed` undefined until close() */
  get closed() {
    return this.#closed || undefined;
  }

  getPrompt() {
    return this.#prompt;
  }

  setPrompt(prompt) {
    this.#prompt = prompt;
  }

  /* { rows, cols }: where the cursor is, counting the prompt, wide characters and wrapping */
  getCursorPos() {
    return this.#displayPos(this.#prompt + this.line.slice(0, this.cursor));
  }

  prompt(preserveCursor) {
    if(this.#paused) this.resume();

    if(this.terminal) {
      if(!preserveCursor) this.cursor = 0;

      this.#refreshLine();
    } else put(this.#output, this.#prompt);
  }

  question(query, options, cb) {
    if(typeof options == 'function') [cb, options] = [options, {}];
    if(this.#closed) throw useAfterClose();
    if(typeof cb != 'function') return;

    const { signal } = options ?? {};

    if(signal?.aborted) return;

    if(this.#question) return this.prompt();

    const old = this.#prompt;
    const q = (this.#question = answer => {
      this.#question = null;
      this.#prompt = old;
      cb(answer);
    });

    signal?.addEventListener('abort', () => {
      if(this.#question === q) {
        this.#question = null;
        this.#prompt = old;
      }
    }, { once: true });

    this.#prompt = query;
    this.prompt();
  }

  /* write(data): text as if typed; write(null, key) or write(data, key) presses a key (terminal mode) */
  write(data, key) {
    if(this.#closed) throw useAfterClose();
    if(this.#paused) this.resume();

    if(this.terminal) this.#ttyWrite(typeof data == 'string' ? data : '', key);
    else if(typeof data == 'string') this.#text(data);
  }

  pause() {
    if(this.#paused) return this;

    this.#paused = true;
    this.#input.pause?.();
    this.emit('pause');

    return this;
  }

  resume() {
    if(!this.#paused) return this;

    this.#paused = false;
    this.#input.resume?.();
    this.#wake?.();
    this.emit('resume');

    return this;
  }

  close() {
    if(this.#closed) return;

    this.pause();
    this.#closed = true;
    this.#stop?.();
    this.#wake?.();
    clearTimeout(this.#keyTimer);

    if(this.#rawFd >= 0) {
      ttySetRaw(this.#rawFd, true);
      this.#rawFd = -1;
    }

    this.emit('close');
  }

  [Symbol.asyncIterator]() {
    const queue = [];
    let wake = null, done = this.#closed;

    this.on('line', line => {
      queue.push(line);
      wake?.();
    });
    this.on('close', () => {
      done = true;
      wake?.();
    });

    return {
      next: async () => {
        while(!queue.length && !done) await new Promise(r => (wake = r));

        return queue.length ? { value: queue.shift(), done: false } : { value: undefined, done: true };
      },
      return: async () => {
        this.close();

        return { value: undefined, done: true };
      },
      [Symbol.asyncIterator]() {
        return this;
      },
    };
  }

  /* ---- line mode: feed decoded text to the line splitter ---- */

  /* a `\n` right after a chunk-final `\r` is the same break */
  #text(s) {
    if(this.#sawReturn && s) {
      if(s[0] == '\n') s = s.slice(1);
      this.#sawReturn = false;
    }

    const re = /\r\n|\n|\r/g;
    const lines = [];
    let last = 0, m;

    while((m = re.exec(s))) {
      lines.push(this.#buffer + s.slice(last, m.index));
      this.#buffer = '';
      last = re.lastIndex;
    }

    this.#buffer += s.slice(last);

    if(s.endsWith('\r')) this.#sawReturn = true;

    for(const line of lines) this.#onLine(line);
  }

  #onLine(line) {
    if(this.#question) this.#question(line);
    else this.emit('line', line);
  }

  /* bytes: the decoder holds a multi-byte char cut at the end of a chunk until the next one */
  #bytes(chunk) {
    const s = this.#decoder.decode(bytesOf(chunk), { stream: true });

    if(s) this.#chunk(s);
  }

  #chunk(chunk) {
    if(typeof chunk == 'string') {
      if(this.terminal) this.#keys(chunk);
      else this.#text(chunk);
    } else if(chunk) this.#bytes(chunk);
  }

  #end() {
    /* a character cut off at the end is dropped, as node does */
    this.#decoder.decode();

    if(this.#buffer) {
      const rest = this.#buffer;

      this.#buffer = '';
      this.#onLine(rest);
    }

    this.close();
  }

  #start() {
    const input = this.#input;

    if(this.#closed) return;

    if(typeof input.fileno == 'function') {
      const fd = input.fileno();
      const buf = new ArrayBuffer(65536);
      const onReadable = () => {
        const n = ttyRead(fd, buf, 0, buf.byteLength);

        if(n > 0) this.#bytes(buf.slice(0, n));
        else if(n != -EAGAIN) {
          setReadHandler(fd, null);
          this.#end();
        }
      };

      if(this.terminal && isatty(fd)) {
        ttySetRaw(fd);
        this.#rawFd = fd;
      }

      this.#stop = () => setReadHandler(fd, null);
      this.on('pause', () => setReadHandler(fd, null));
      this.on('resume', () => !this.#closed && setReadHandler(fd, onReadable));
      if(!this.#paused) setReadHandler(fd, onReadable);
    } else if(typeof input.on == 'function' && !input.getReader && !input[Symbol.asyncIterator]) {
      input.on('data', chunk => this.#chunk(chunk));
      input.on('end', () => this.#end());
      input.resume?.();
    } else {
      const { next, cancel } = pullSource(input);

      this.#stop = cancel;

      (async () => {
        try {
          for(;;) {
            while(this.#paused && !this.#closed) await new Promise(r => (this.#wake = r));

            if(this.#closed) return;

            const { value, done } = await next();

            if(done) break;

            this.#chunk(value);
          }

          this.#end();
        } catch(e) {
          this.emit('error', e);
        }
      })();
    }
  }

  /* ---- terminal mode: keystrokes edit `line` ---- */

  /* decode keystrokes; an unfinished ESC sequence waits `escapeCodeTimeout` ms for the rest */
  #keys(str) {
    clearTimeout(this.#keyTimer);

    const { keys, rest } = decodeKeys(this.#keyRest + str);

    this.#keyRest = rest;

    if(rest)
      this.#keyTimer = setTimeout(() => {
        const { keys } = decodeKeys(this.#keyRest, true);

        this.#keyRest = '';

        for(const { s, key } of keys) this.#ttyWrite(s, key);
      }, this.#escapeTimeout);

    for(const { s, key } of keys) this.#ttyWrite(s, key);
  }

  /* { rows, cols } of the end of `str` on a screen `columns` wide */
  #displayPos(str) {
    const col = this.columns;
    let offset = 0, rows = 0;

    for(const ch of stripAnsi(str)) {
      if(ch == '\n') {
        rows += Math.ceil(offset / col) || 1;
        offset = 0;
      } else if(ch == '\t') {
        offset += this.#tabSize - (offset % this.#tabSize);
      } else {
        const width = charWidth(ch);

        if(width < 2) offset += width;
        else {
          if((offset + 1) % col === 0) offset++;

          offset += 2;
        }
      }
    }

    const cols = offset % col;

    rows += (offset - cols) / col;

    return { cols, rows };
  }

  #refreshLine() {
    const line = this.#prompt + this.line;
    const dispPos = this.#displayPos(line);
    const cursorPos = this.getCursorPos();

    if(this.#prevRows > 0) moveCursor(this.#output, 0, -this.#prevRows);

    cursorTo(this.#output, 0);
    clearScreenDown(this.#output);
    put(this.#output, line);

    if(dispPos.cols === 0) put(this.#output, ' ');

    cursorTo(this.#output, cursorPos.cols);

    const diff = dispPos.rows - cursorPos.rows;

    if(diff > 0) moveCursor(this.#output, 0, -diff);

    this.#prevRows = cursorPos.rows;
  }

  /* move the cursor by `dx` units of `line`, clamped; a plain move when it stays on its row */
  #moveCursor(dx) {
    const old = this.getCursorPos();

    this.cursor = Math.max(0, Math.min(this.line.length, this.cursor + dx));

    const now = this.getCursorPos();

    if(old.rows === now.rows) moveCursor(this.#output, now.cols - old.cols, 0);
    else this.#refreshLine();
  }

  #insert(c) {
    if(this.cursor < this.line.length) {
      this.line = this.line.slice(0, this.cursor) + c + this.line.slice(this.cursor);
      this.cursor += c.length;
      this.#refreshLine();
    } else {
      this.line += c;
      this.cursor += c.length;

      if(this.getCursorPos().cols === 0) this.#refreshLine();
      else put(this.#output, c);
    }
  }

  #edit(line, cursor, killed) {
    if(killed) this.#killed = killed;

    this.line = line;
    this.cursor = cursor;
    this.#refreshLine();
  }

  clearLine() {
    this.#moveCursor(Infinity);
    put(this.#output, '\r\n');
    this.line = '';
    this.cursor = 0;
    this.#prevRows = 0;
  }

  #enter() {
    if(this.line.trim().length && this.#historySize > 0) {
      if(this.history[0] !== this.line) {
        if(this.#removeDuplicates) {
          const at = this.history.indexOf(this.line);

          if(at !== -1) this.history.splice(at, 1);
        }

        this.history.unshift(this.line);

        if(this.history.length > this.#historySize) this.history.pop();
      }

      this.#historyIndex = -1;
      this.emit('history', this.history);
    }

    const line = this.line;

    this.clearLine();
    this.#onLine(line);
  }

  #historyPrev(useSearch) {
    if(this.#historyIndex < this.history.length && this.history.length) {
      if(useSearch && this.#search === null) this.#search = this.line.slice(0, this.cursor);

      const search = (useSearch && this.#search) || '';
      let index = this.#historyIndex + 1;

      while(index < this.history.length && (!this.history[index].startsWith(search) || this.line === this.history[index])) index++;

      this.line = index === this.history.length ? search : this.history[index];
      this.#historyIndex = index;
      this.cursor = this.line.length;
      this.#refreshLine();
    }
  }

  #historyNext(useSearch) {
    if(this.#historyIndex >= 0 && this.history.length) {
      const search = (useSearch && this.#search) || '';
      let index = this.#historyIndex - 1;

      while(index >= 0 && (!this.history[index].startsWith(search) || this.line === this.history[index])) index--;

      this.line = index === -1 ? search : this.history[index];
      this.#historyIndex = index;
      this.cursor = this.line.length;
      this.#refreshLine();
    }
  }

  #wordLeft() {
    this.#moveCursor(-this.line.slice(0, this.cursor).match(/(?:[^\w\s]+|\w+|)\s*$/)[0].length);
  }

  #wordRight() {
    const m = this.line.slice(this.cursor).match(/^(?:\s+|[^\w\s]+|\w+)\s*/);

    if(m) this.#moveCursor(m[0].length);
  }

  #deleteWordLeft() {
    const lead = this.line.slice(0, this.cursor);
    const cut = lead.match(/(?:[^\w\s]+|\w+|)\s*$/)[0];
    const kept = lead.slice(0, lead.length - cut.length);

    this.#edit(kept + this.line.slice(this.cursor), kept.length, cut);
  }

  #deleteWordRight() {
    const trail = this.line.slice(this.cursor);
    const cut = trail.match(/^(?:\s+|\W+|\w+)\s*/)?.[0] ?? '';

    this.#edit(this.line.slice(0, this.cursor) + trail.slice(cut.length), this.cursor, cut);
  }

  #tabComplete(lastWasTab) {
    const completer = this.#completer;

    this.pause();

    const string = this.line.slice(0, this.cursor);
    const done = (err, value) => {
      this.resume();

      if(err) put(this.#output, `Tab completion error: ${err}`);
      else this.#completions(lastWasTab, value);
    };

    if(completer.length >= 2) return completer(string, done);

    let value;

    try {
      value = completer(string);
    } catch(e) {
      return done(e);
    }

    if(typeof value?.then == 'function') value.then(v => done(null, v), done);
    else done(null, value);
  }

  #completions(lastWasTab, value) {
    const [completions, completeOn] = value ?? [];

    if(!completions?.length) return;

    const prefix = commonPrefix(completions.filter(e => e !== ''));

    if(prefix.startsWith(completeOn) && prefix.length > completeOn.length) return this.#insert(prefix.slice(completeOn.length));

    if(!completeOn.startsWith(prefix)) {
      this.line = this.line.slice(0, this.cursor - completeOn.length) + prefix + this.line.slice(this.cursor);
      this.cursor = this.cursor - completeOn.length + prefix.length;

      return this.#refreshLine();
    }

    if(!lastWasTab) return;

    const widths = completions.map(stringWidth);
    const width = Math.max(...widths) + 2;
    let maxColumns = Math.floor(this.columns / width) || 1;
    let out = '\r\n', lineIndex = 0, whitespace = 0;

    if(maxColumns === Infinity) maxColumns = 1;

    completions.forEach((completion, i) => {
      if(completion === '' || lineIndex === maxColumns) {
        out += '\r\n';
        lineIndex = 0;
        whitespace = 0;
      } else out += ' '.repeat(whitespace);

      if(completion !== '') {
        out += completion;
        whitespace = width - widths[i];
        lineIndex++;
      } else out += '\r\n';
    });

    if(lineIndex !== 0) out += '\r\n\r\n';

    put(this.#output, out);
    this.#refreshLine();
  }

  /* one keypress: `s` the text, `key` { name, ctrl, meta, shift } (absent for typed text) */
  #ttyWrite(s, key) {
    const prev = this.#prevKey;

    key ??= {};
    this.#prevKey = key;

    if(key.name !== 'up' && key.name !== 'down') this.#search = null;
    if(key.name === 'escape') return;

    if(key.ctrl && key.shift) {
      if(key.name === 'backspace') this.#edit(this.line.slice(this.cursor), 0, this.line.slice(0, this.cursor));
      else if(key.name === 'delete') this.#edit(this.line.slice(0, this.cursor), this.cursor, this.line.slice(this.cursor));
    } else if(key.ctrl) {
      switch (key.name) {
        case 'c':
          if(this.listenerCount('SIGINT') > 0) this.emit('SIGINT');
          else this.close();
          break;
        case 'h':
          this.#deleteLeft();
          break;
        case 'd':
          if(this.cursor === 0 && this.line.length === 0) this.close();
          else if(this.cursor < this.line.length) this.#deleteRight();
          break;
        case 'u':
          this.#edit(this.line.slice(this.cursor), 0, this.line.slice(0, this.cursor));
          break;
        case 'k':
          this.#edit(this.line.slice(0, this.cursor), this.cursor, this.line.slice(this.cursor));
          break;
        case 'a':
          this.#moveCursor(-Infinity);
          break;
        case 'e':
          this.#moveCursor(Infinity);
          break;
        case 'b':
          this.#moveLeft();
          break;
        case 'f':
          this.#moveRight();
          break;
        case 'l':
          cursorTo(this.#output, 0, 0);
          clearScreenDown(this.#output);
          this.#refreshLine();
          break;
        case 'n':
          this.#historyNext(false);
          break;
        case 'p':
          this.#historyPrev(false);
          break;
        case 'z':
          if(this.listenerCount('SIGTSTP') > 0) this.emit('SIGTSTP');
          break;
        case 'w':
        case 'backspace':
          this.#deleteWordLeft();
          break;
        case 'delete':
          this.#deleteWordRight();
          break;
        case 'left':
          this.#wordLeft();
          break;
        case 'right':
          this.#wordRight();
          break;
        case 'y':
          if(this.#killed) this.#insert(this.#killed);
          break;
      }
    } else if(key.meta) {
      switch (key.name) {
        case 'b':
          this.#wordLeft();
          break;
        case 'f':
          this.#wordRight();
          break;
        case 'd':
        case 'delete':
          this.#deleteWordRight();
          break;
        case 'backspace':
          this.#deleteWordLeft();
          break;
      }
    } else {
      switch (key.name) {
        case 'return':
          this.#sawReturnAt = Date.now();
          this.#enter();
          break;
        case 'enter':
          if(this.#sawReturnAt === null || Date.now() - this.#sawReturnAt > this.#crlfDelay) this.#enter();

          this.#sawReturnAt = null;
          break;
        case 'backspace':
          this.#deleteLeft();
          break;
        case 'delete':
          this.#deleteRight();
          break;
        case 'left':
          this.#moveLeft();
          break;
        case 'right':
          this.#moveRight();
          break;
        case 'home':
          this.#moveCursor(-Infinity);
          break;
        case 'end':
          this.#moveCursor(Infinity);
          break;
        case 'up':
          this.#historyPrev(true);
          break;
        case 'down':
          this.#historyNext(true);
          break;
        case 'tab':
          if(typeof this.#completer == 'function') {
            this.#tabComplete(prev?.name === 'tab');
            break;
          }
        // falls through
        default:
          if(typeof s == 'string' && s && !s.startsWith('\x1b')) this.#typed(s);
      }
    }
  }

  /* text from the keyboard or write(): inserted, a line ending presses enter */
  #typed(s) {
    const re = /\r\n|\n|\r/g;
    let last = 0, m;

    while((m = re.exec(s))) {
      if(m.index > last) this.#insert(s.slice(last, m.index));

      this.#enter();
      last = re.lastIndex;
    }

    if(last < s.length) this.#insert(s.slice(last));
  }

  #moveLeft() {
    if(this.cursor > 0) this.#moveCursor(-charLengthLeft(this.line, this.cursor));
  }

  #moveRight() {
    if(this.cursor < this.line.length) this.#moveCursor(charLengthAt(this.line, this.cursor));
  }

  #deleteLeft() {
    if(this.cursor > 0 && this.line.length > 0) {
      const size = charLengthLeft(this.line, this.cursor);

      this.#edit(this.line.slice(0, this.cursor - size) + this.line.slice(this.cursor), this.cursor - size);
    }
  }

  #deleteRight() {
    if(this.cursor < this.line.length) this.#edit(this.line.slice(0, this.cursor) + this.line.slice(this.cursor + charLengthAt(this.line, this.cursor)), this.cursor);
  }
}

/* createInterface(input, output?, completer?, terminal?) or createInterface({ input, output, prompt, signal }) */
export function createInterface(...args) {
  return new Interface(...args);
}

class PromisesInterface extends Interface {
  question(query, options = {}) {
    return new Promise((resolve, reject) => {
      const { signal } = options;

      if(signal?.aborted) return reject(abortError(signal));

      signal?.addEventListener('abort', () => reject(abortError(signal)), { once: true });
      super.question(query, options, resolve);
    });
  }
}

export const promises = {
  Interface: PromisesInterface,
  createInterface: (...args) => new PromisesInterface(...args),
};

const writeSeq = (stream, seq, cb) => (stream == null || (typeof stream.write != 'function' && typeof stream.puts != 'function' && typeof stream.getWriter != 'function') ? (typeof cb == 'function' && Promise.resolve().then(cb), true) : put(stream, seq, cb));

/* cursorTo(stream, x, y?, callback?): move the cursor to column x (and row y), 0-based */
export function cursorTo(stream, x, y, callback) {
  if(typeof y == 'function') [callback, y] = [y, undefined];

  return writeSeq(stream, typeof y != 'number' ? `\x1b[${x + 1}G` : `\x1b[${y + 1};${x + 1}H`, callback);
}

/* moveCursor(stream, dx, dy, callback?): move the cursor relative to where it is */
export function moveCursor(stream, dx, dy, callback) {
  let seq = '';

  if(dx < 0) seq += `\x1b[${-dx}D`;
  else if(dx > 0) seq += `\x1b[${dx}C`;

  if(dy < 0) seq += `\x1b[${-dy}A`;
  else if(dy > 0) seq += `\x1b[${dy}B`;

  return writeSeq(stream, seq, callback);
}

/* clearLine(stream, dir, callback?): erase left (-1), right (1) or all (0) of the line */
export function clearLine(stream, dir, callback) {
  return writeSeq(stream, dir < 0 ? '\x1b[1K' : dir > 0 ? '\x1b[0K' : '\x1b[2K', callback);
}

/* clearScreenDown(stream, callback?): erase from the cursor to the end of the screen */
export function clearScreenDown(stream, callback) {
  return writeSeq(stream, '\x1b[0J', callback);
}

/* the module object, what import x from 'readline' gives in Node */
export default { Interface, clearLine, clearScreenDown, createInterface, cursorTo, emitKeypressEvents, moveCursor, promises };
