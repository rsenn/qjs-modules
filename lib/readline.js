/* readline.js: Node's `readline` (also Bun's, Deno's): line-by-line input and cursor helpers.
 * depends on: events (EventEmitter), os (fd reads), textcode (UTF-8 decode).
 * rule: no Node streams; `input` is a ReadableStream, an async iterable of string or bytes,
 *       a 'data'/'end' emitter, or a FILE (`process.stdin`); no raw-mode line editing.
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
 * `terminal` is always false: on a tty the kernel's own line editing applies. Missing:
 * emitKeypressEvents, history, completer.
 */
import { EventEmitter } from 'events';
import { read as osRead, setReadHandler } from 'os';
import { TextDecoder } from 'textcode';

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });
const useAfterClose = () => nodeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed');
const abortError = signal => Object.assign(new Error('The operation was aborted', { cause: signal.reason }), { name: 'AbortError', code: 'ABORT_ERR' });

/* write `s` to a Node writable, a FILE or a WHATWG WritableStream; true when written */
function put(out, s, cb) {
  if(!out) return true;

  if(typeof out.puts == 'function') out.puts(s);
  else if(typeof out.write == 'function') return out.write(s, cb);
  else if(typeof out.getWriter == 'function') (out[kWriter] ??= out.getWriter()).write(s);
  else return true;

  if(typeof cb == 'function') Promise.resolve().then(cb);

  return true;
}

const kWriter = Symbol('readline.writer');

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

export class Interface extends EventEmitter {
  #input;
  #output;
  #prompt;
  #paused = false;
  #closed = false;
  #buffer = '';
  #sawReturn = false;
  #tail = new Uint8Array(0);
  #decoder = new TextDecoder();
  #question = null;
  #wake = null;
  #stop = null;

  constructor(input, output, completer, terminal) {
    super();

    const options = input && typeof input == 'object' && 'input' in input ? input : { input, output, completer, terminal };

    const src = options.input;

    if(!src || typeof src != 'object' || !(typeof src.getReader == 'function' || typeof src[Symbol.asyncIterator] == 'function' || typeof src.fileno == 'function' || typeof src.on == 'function'))
      throw new TypeError('The "input" argument must be a ReadableStream, async iterable, FILE or emitter of data');

    this.#input = options.input;
    this.#output = options.output;
    this.#prompt = options.prompt ?? '> ';
    this.terminal = false;
    this.line = '';
    this.history = [];

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

  getCursorPos() {
    return { rows: 0, cols: this.#prompt.length };
  }

  prompt() {
    if(this.#paused) this.resume();

    put(this.#output, this.#prompt);
  }

  question(query, options, cb) {
    if(typeof options == 'function') [cb, options] = [options, {}];
    if(this.#closed) throw useAfterClose();
    if(typeof cb != 'function') return;

    const { signal } = options ?? {};

    if(signal?.aborted) return;

    if(this.#question) return this.prompt();

    const q = (this.#question = answer => {
      this.#question = null;
      cb(answer);
    });

    signal?.addEventListener('abort', () => this.#question === q && (this.#question = null), { once: true });

    if(this.#paused) this.resume();

    put(this.#output, query);
  }

  write(data) {
    if(this.#closed) throw useAfterClose();
    if(this.#paused) this.resume();
    if(typeof data == 'string') this.#text(data);
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

  /* feed decoded text to the line splitter; a `\n` right after a chunk-final `\r` is the same break */
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

    for(const line of lines) this.#line(line);
  }

  #line(line) {
    this.line = '';

    if(this.#question) this.#question(line);
    else this.emit('line', line);
  }

  /* bytes: decode up to the last line break so a multi-byte char is never cut */
  #bytes(chunk) {
    let u8 = bytesOf(chunk);

    if(this.#tail.length) {
      const joined = new Uint8Array(this.#tail.length + u8.length);

      joined.set(this.#tail);
      joined.set(u8, this.#tail.length);
      u8 = joined;
    }

    let cut = u8.length;

    while(cut > 0 && u8[cut - 1] != 10 && u8[cut - 1] != 13) cut--;

    this.#tail = u8.slice(cut);

    if(cut) this.#text(this.#decoder.decode(u8.subarray(0, cut)));
  }

  #chunk(chunk) {
    if(typeof chunk == 'string') this.#text(chunk);
    else if(chunk) this.#bytes(chunk);
  }

  #end() {
    if(this.#tail.length) this.#text(this.#decoder.decode(this.#tail));

    if(this.#buffer) {
      const rest = this.#buffer;

      this.#buffer = '';
      this.#line(rest);
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
        const n = osRead(fd, buf, 0, buf.byteLength);

        if(n > 0) this.#bytes(buf.slice(0, n));
        else {
          setReadHandler(fd, null);
          this.#end();
        }
      };

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
