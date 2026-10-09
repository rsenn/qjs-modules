/* node/child_process.js: Node's `child_process` shapes on top of the
 * native `child_process` (spawn(2)/waitpid(2) primitives).
 * loaded for `node:child_process`; the bare name stays the native module.
 *
 * ```js
 * import { spawn, execSync, execFile } from 'node:child_process';
 * const c = spawn('ls', ['-l']);
 * c.stdout.on('data', d => {});
 * c.on('close', (code, signal) => {});
 * ```
 *
 * Node API: spawn spawnSync exec execSync execFile execFileSync fork
 * ChildProcess. streams are small EventEmitter objects (data/end,
 * write/end, async iterator), not Node streams.
 *
 * limits:
 *   fork()      no IPC channel (send/disconnect/'message' are absent)
 *   argv0, uid, gid, detached, windowsHide  ignored
 *   stdin writes block while the pipe is full
 *   ref()/unref() are no-ops
 */
import * as native from 'child_process' with { type: 'native' };
import * as os from 'os';
import { EventEmitter } from 'node:events';
import { Buffer } from 'node:buffer';
import { signum } from 'misc';
import { poll, POLLIN, POLLOUT } from 'sockets';
import process from 'process';

// the native module reaches os.signal() through the global `os` (SIGCHLD handler)
if(!('os' in globalThis)) Object.defineProperty(globalThis, 'os', { value: os, writable: true, configurable: true });

const kMaxBuffer = 1024 * 1024;
const defer = fn => Promise.resolve().then(fn);

/* ---- errors ---- */

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const argType = (name, expected, value) =>
  nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. Received ${value === null ? 'null' : value === undefined ? 'undefined' : typeof value == 'object' ? `an instance of ${value.constructor?.name ?? 'Object'}` : `type ${typeof value} (${String(value)})`}`);

const errnoError = (code, errno, syscall, path, spawnargs) => {
  const e = new Error(`${syscall} ${path ?? ''} ${code}`.replace(/ +/g, ' '));
  return Object.assign(e, { errno, code, syscall, path, spawnargs });
};

/* ---- argument normalisation ---- */

const isExecutable = file => {
  const [st, err] = os.stat(file);
  return err === 0 && (st.mode & os.S_IFMT) == os.S_IFREG && (st.mode & 0o111) != 0 ? true : err === 0 ? 'EACCES' : false;
};

/* lookup: finds `file` like execvp(); returns path, or an error code */
const lookup = (file, env, cwd) => {
  const abs = f => (f[0] == '/' || !cwd ? f : `${cwd}/${f}`);
  const dirs = file.includes('/') ? [''] : (env.PATH ?? '/usr/local/bin:/usr/bin:/bin').split(':');
  let code = 'ENOENT';

  for(const dir of dirs) {
    const f = abs(dir ? `${dir}/${file}` : file);
    const r = isExecutable(f);
    if(r === true) return f;
    if(r === 'EACCES') code = 'EACCES';
  }

  return code;
};

const stdioItem = (item, i) => {
  if(item === undefined || item === null || item === 'pipe' || item === 'overlapped') return 'pipe';
  if(item === 'ignore' || item === 'inherit') return item;
  if(item === 'ipc') throw nodeError(Error, 'ERR_IPC_UNSUPPORTED', 'stdio "ipc" is not supported');
  if(typeof item == 'number') return item;
  if(typeof item == 'object' && typeof item.fd == 'number') return item.fd;
  if(typeof item == 'object' && typeof item.fileno == 'function') return item.fileno();
  throw nodeError(TypeError, 'ERR_INVALID_SYNC_FORK_INPUT', `Incorrect value of stdio option: ${String(item)}`);
};

const normalizeStdio = stdio => {
  if(stdio === undefined || typeof stdio == 'string') return [0, 1, 2].map(i => stdioItem(stdio, i));
  if(!Array.isArray(stdio)) throw argType('options.stdio', 'of type string or an instance of Array', stdio);
  const a = stdio.map(stdioItem);
  while(a.length < 3) a.push('pipe');
  return a;
};

/* normalize: (file, args?, options?) -> { file, args, options, command } */
function normalize(file, args, options) {
  if(typeof file != 'string') throw argType('file', 'of type string', file);
  if(file.length === 0) throw nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'file' cannot be empty. Received ''");

  if(args === undefined || args === null) args = [];
  else if(!Array.isArray(args)) {
    if(typeof args != 'object') throw argType('args', 'an instance of Array', args);
    options = args;
    args = [];
  }

  options = options === undefined || options === null ? {} : options;
  if(typeof options != 'object') throw argType('options', 'of type object', options);
  args = args.map(String);

  let { shell } = options;
  let command = [file, ...args].join(' ');

  if(shell) {
    if(typeof shell != 'string') shell = '/bin/sh';
    args = ['-c', command];
    file = shell;
  }

  const env = options.env ?? process.env;
  const stdio = normalizeStdio(options.stdio);

  return { file, args, command, options: { ...options, env, stdio } };
}

/* start: runs the native spawn; returns { handle } or { error } */
function start(file, args, options) {
  const found = lookup(file, options.env, options.cwd);
  if(found == 'ENOENT' || found == 'EACCES') return { error: errnoError(found, found == 'ENOENT' ? -2 : -13, `spawn ${file}`, file, args) };

  const env = {};
  for(const [k, v] of Object.entries(options.env)) if(v !== undefined) env[k] = String(v);

  const handle = native.spawn(file, args, { env, cwd: options.cwd, stdio: options.stdio, usePath: !file.includes('/') });
  return { handle };
}

/* ---- pipe streams ---- */

const READ_SIZE = 65536;

class PipeReader extends EventEmitter {
  #fd;
  #encoding;
  #pending = [];
  #flowing = false;
  #waiters = [];
  #tail = null;
  readable = true;
  destroyed = false;
  readableEnded = false;

  constructor(fd) {
    super();
    this.#fd = fd;
    const buf = new ArrayBuffer(READ_SIZE);
    os.setReadHandler(fd, () => {
      const n = os.read(fd, buf, 0, READ_SIZE);
      if(n > 0) this.#push(Buffer.from(buf.slice(0, n)));
      else this.#finish();
    });
  }

  get fd() {
    return this.#fd;
  }

  setEncoding(encoding = 'utf8') {
    this.#encoding = encoding;
    return this;
  }

  #decode(chunk) {
    if(!this.#encoding) return chunk;
    if(this.#tail) chunk = Buffer.concat([this.#tail, chunk]);
    this.#tail = null;

    if(/^utf-?8$/i.test(this.#encoding)) {
      // hold back an incomplete trailing multibyte sequence
      let i = chunk.length - 1,
        back = 0;

      while(i >= 0 && back < 3 && (chunk[i] & 0xc0) == 0x80) (i--, back++);

      if(i >= 0 && chunk[i] >= 0xc0) {
        const need = chunk[i] >= 0xf0 ? 4 : chunk[i] >= 0xe0 ? 3 : 2;

        if(chunk.length - i < need) {
          this.#tail = chunk.subarray(i);
          chunk = chunk.subarray(0, i);
        }
      }
    }

    return chunk.toString(this.#encoding);
  }

  #push(chunk) {
    if(this.#waiters.length) return this.#waiters.shift().resolve({ value: this.#decode(chunk), done: false });
    if(this.#flowing) this.emit('data', this.#decode(chunk));
    else this.#pending.push(chunk);
  }

  #finish() {
    if(this.destroyed) return;
    os.setReadHandler(this.#fd, null);
    os.close(this.#fd);
    this.destroyed = true;
    this.readable = false;

    if(this.#tail) {
      const t = this.#tail.toString(this.#encoding);
      this.#tail = null;
      if(this.#flowing) this.emit('data', t);
    }

    if(this.#flowing || !this.#pending.length) this.#end();
    else this.#endAfterDrain = true;
  }

  #endAfterDrain = false;

  #end() {
    this.readableEnded = true;
    for(const w of this.#waiters.splice(0)) w.resolve({ value: undefined, done: true });
    this.emit('end');
    this.emit('close');
  }

  resume() {
    this.#flowing = true;
    const pending = this.#pending.splice(0);
    for(const c of pending) this.emit('data', this.#decode(c));
    if(this.#endAfterDrain) this.#end();
    return this;
  }

  pause() {
    this.#flowing = false;
    return this;
  }

  on(type, fn) {
    super.on(type, fn);
    if(type == 'data') defer(() => this.resume());
    return this;
  }

  addListener(type, fn) {
    return this.on(type, fn);
  }

  pipe(dest) {
    this.on('data', d => dest.write(d));
    this.on('end', () => dest.end?.());
    return dest;
  }

  destroy() {
    if(!this.destroyed) {
      os.setReadHandler(this.#fd, null);
      os.close(this.#fd);
      this.destroyed = true;
      this.readable = false;
      this.emit('close');
    }

    return this;
  }

  [Symbol.asyncIterator]() {
    return {
      next: () => {
        if(this.#pending.length) return Promise.resolve({ value: this.#decode(this.#pending.shift()), done: false });
        if(this.readableEnded || this.destroyed) return Promise.resolve({ value: undefined, done: true });
        return new Promise((resolve, reject) => this.#waiters.push({ resolve, reject }));
      },
      return: () => {
        this.destroy();
        return Promise.resolve({ value: undefined, done: true });
      },
      [Symbol.asyncIterator]() {
        return this;
      },
    };
  }
}

const toBytes = (chunk, encoding) => {
  if(typeof chunk == 'string') return Buffer.from(chunk, encoding);
  if(ArrayBuffer.isView(chunk)) return Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);
  throw argType('chunk', 'of type string or an instance of Buffer, TypedArray, or DataView', chunk);
};

class PipeWriter extends EventEmitter {
  #fd;
  writable = true;
  destroyed = false;
  writableEnded = false;

  constructor(fd) {
    super();
    this.#fd = fd;
  }

  get fd() {
    return this.#fd;
  }

  write(chunk, encoding, cb) {
    if(typeof encoding == 'function') [cb, encoding] = [encoding, undefined];

    if(this.writableEnded) {
      const e = nodeError(Error, 'ERR_STREAM_WRITE_AFTER_END', 'write after end');
      defer(() => (cb?.(e), this.emit('error', e)));
      return false;
    }

    const bytes = toBytes(chunk, encoding);
    let off = 0;

    while(off < bytes.length) {
      const n = os.write(this.#fd, bytes.buffer, bytes.byteOffset + off, bytes.length - off);

      if(n <= 0) {
        const e = errnoError('EPIPE', -32, 'write');
        defer(() => (cb?.(e), this.emit('error', e)));
        return false;
      }

      off += n;
    }

    if(cb) defer(() => cb(null));
    return true;
  }

  end(chunk, encoding, cb) {
    if(typeof chunk == 'function') [cb, chunk] = [chunk, undefined];
    if(typeof encoding == 'function') [cb, encoding] = [encoding, undefined];
    if(chunk !== undefined && chunk !== null) this.write(chunk, encoding);
    if(!this.writableEnded) {
      this.writableEnded = true;
      this.writable = false;
      os.close(this.#fd);
      this.destroyed = true;
      defer(() => {
        this.emit('finish');
        this.emit('close');
        cb?.();
      });
    }

    return this;
  }

  destroy() {
    return this.end();
  }

  cork() {}
  uncork() {}
}

/* ---- ChildProcess ---- */

const live = new Set();
let pollTimer;

const reapAll = () => {
  for(const c of [...live]) c._poll();

  if(!live.size) {
    clearInterval(pollTimer);
    pollTimer = undefined;
  }
};

const track = c => {
  live.add(c);
  pollTimer ??= setInterval(reapAll, 50);
};

export class ChildProcess extends EventEmitter {
  #handle;
  #timer;
  #closed = false;
  #streams = [];

  constructor() {
    super();
    this.pid = undefined;
    this.connected = false;
    this.exitCode = null;
    this.signalCode = null;
    this.killed = false;
    this.spawnfile = undefined;
    this.spawnargs = [];
    this.stdin = this.stdout = this.stderr = null;
    this.stdio = [null, null, null];
  }

  /* _spawn: starts the process; called by spawn() */
  _spawn(file, args, options, display) {
    this.spawnfile = display.file;
    this.spawnargs = [display.file, ...display.args];
    const { handle, error } = start(file, args, options);

    if(error) {
      this.exitCode = -2;
      defer(() => {
        this.emit('error', error);
        this.#close();
      });
      return;
    }

    this.#handle = handle;
    this.pid = handle.pid;
    const fds = handle.stdio;

    for(let i = 0; i < 3; i++) {
      if(options.stdio[i] != 'pipe' || fds?.[i] == null || fds[i] < 0) continue;
      const s = i == 0 ? new PipeWriter(fds[i]) : new PipeReader(fds[i]);
      this.stdio[i] = s;
      if(i) this.#streams.push(s);
    }

    [this.stdin, this.stdout, this.stderr] = this.stdio;

    handle.onExit = (code, signal) => this.#onExit(code, signal);
    track(this);

    for(const s of this.#streams) s.on('close', () => this.#maybeClose());
    // resume pipes nobody listens to so the child never blocks on a full pipe
    defer(() => {
      for(const s of this.#streams) if(!s.listenerCount('data') && !s.readableEnded) s.resume();
      this.emit('spawn');
    });

    if(options.timeout > 0) this.#timer = setTimeout(() => this.kill(options.killSignal ?? 'SIGTERM'), options.timeout);

    const { signal } = options;
    if(signal) {
      const abort = () => {
        this.kill(options.killSignal ?? 'SIGTERM');
        this.emit('error', Object.assign(new Error('The operation was aborted', { cause: signal.reason }), { name: 'AbortError', code: 'ABORT_ERR' }));
      };
      if(signal.aborted) defer(abort);
      else signal.addEventListener('abort', abort, { once: true });
    }
  }

  _poll() {
    if(this.#handle && this.exitCode === null && this.signalCode === null) this.#handle.wait(native.WNOHANG);
  }

  #onExit(code, signal) {
    if(this.#timer) clearTimeout(this.#timer);
    live.delete(this);
    this.exitCode = code;
    this.signalCode = signal;
    this.emit('exit', code, signal);
    this.#maybeClose();
  }

  #maybeClose() {
    if(this.#closed || (this.exitCode === null && this.signalCode === null)) return;
    if(this.#streams.every(s => s.readableEnded || s.destroyed)) this.#close();
  }

  #close() {
    if(this.#closed) return;
    this.#closed = true;
    live.delete(this);
    this.emit('close', this.exitCode, this.signalCode);
  }

  kill(signal = 'SIGTERM') {
    let num;
    try {
      num = typeof signal == 'number' ? signal : signum(String(signal));
    } catch(e) {}

    if(!Number.isInteger(num)) throw nodeError(TypeError, 'ERR_UNKNOWN_SIGNAL', `Unknown signal: ${signal}`);
    if(!this.#handle || this.exitCode !== null || this.signalCode !== null) return false;
    const ok = this.#handle.kill(num);
    if(ok) this.killed = true;
    return ok;
  }

  ref() {}
  unref() {}

  [Symbol.dispose]() {
    if(!this.killed) this.kill();
  }
}

/* ---- spawn / exec / execFile / fork ---- */

export function spawn(file, args, options) {
  const n = normalize(file, args, options);
  const c = new ChildProcess();
  c._spawn(n.file, n.args, n.options, { file: n.file, args: n.args });
  return c;
}

const toEncoding = enc => (enc && enc !== 'buffer' && Buffer.isEncoding(enc) ? enc : null);

function collect(child, { encoding, maxBuffer = kMaxBuffer }) {
  const enc = toEncoding(encoding ?? 'utf8');
  const out = { stdout: [], stderr: [], size: { stdout: 0, stderr: 0 }, exceeded: false };

  for(const name of ['stdout', 'stderr']) {
    child[name]?.on('data', d => {
      const b = typeof d == 'string' ? Buffer.from(d) : d;
      out.size[name] += b.length;
      if(out.size[name] > maxBuffer && !out.exceeded) {
        out.exceeded = true;
        child.kill();
      }

      out[name].push(b);
    });
  }

  out.result = name => {
    const b = Buffer.concat(out[name]);
    return enc ? b.toString(enc) : b;
  };

  return out;
}

export function execFile(file, args, options, callback) {
  if(typeof args == 'function') [callback, args, options] = [args, undefined, undefined];
  else if(typeof options == 'function') [callback, options] = [options, undefined];
  if(args && !Array.isArray(args) && typeof args == 'object') [options, args] = [args, undefined];
  if(callback !== undefined && typeof callback != 'function') throw argType('callback', 'of type function', callback);

  const n = normalize(file, args, options);
  const child = new ChildProcess();
  const opts = n.options;
  child._spawn(n.file, n.args, opts, { file: n.file, args: n.args });
  const out = collect(child, opts);
  let done = false;

  const finish = (code, signal, spawnError) => {
    if(done) return;
    done = true;
    const stdout = out.result('stdout'),
      stderr = out.result('stderr');
    if(!callback) return;
    let err = spawnError ?? null;
    if(!err && out.exceeded) err = Object.assign(new RangeError('stdout maxBuffer length exceeded'), { code: 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER' });
    else if(!err && (code !== 0 || signal)) {
      const cmd = opts.shell ? n.command : [file, ...(Array.isArray(args) ? args : [])].join(' ');
      err = Object.assign(new Error(`Command failed: ${cmd}\n${stderr}`), { code: signal ? null : code, killed: child.killed, signal, cmd });
    }

    if(err) Object.assign(err, { stdout, stderr });
    callback(err, stdout, stderr);
  };

  child.on('error', e => finish(null, null, e));
  child.on('close', (code, signal) => finish(code, signal));
  return child;
}

export function exec(command, options, callback) {
  if(typeof options == 'function') [callback, options] = [options, undefined];
  if(typeof command != 'string') throw argType('command', 'of type string', command);
  return execFile(command, [], { ...options, shell: options?.shell ?? true }, callback);
}

/* util.promisify(exec/execFile) resolves { stdout, stderr } */
for(const fn of [exec, execFile])
  Object.defineProperty(fn, Symbol.for('nodejs.util.promisify.custom'), {
    configurable: true,
    value: (...args) => {
      let child;
      const p = new Promise((resolve, reject) => {
        child = fn(...args, (err, stdout, stderr) => (err ? reject(err) : resolve({ stdout, stderr })));
      });
      p.child = child;
      return p;
    },
  });

export function fork(modulePath, args, options) {
  if(typeof modulePath != 'string') throw argType('modulePath', 'of type string', modulePath);
  if(args && !Array.isArray(args)) [options, args] = [args, []];
  options = { ...options };
  const execArgv = options.execArgv ?? process.execArgv ?? [];
  options.stdio ??= options.silent ? 'pipe' : 'inherit';
  return spawn(options.execPath ?? process.execPath, [...execArgv, modulePath, ...(args ?? [])], options);
}

/* ---- synchronous ---- */

const inputBytes = input => (input === undefined || input === null ? null : toBytes(input, 'utf8'));

export function spawnSync(file, args, options) {
  const n = normalize(file, args, options);
  const opts = n.options;
  const result = { status: null, signal: null, output: null, pid: 0, stdout: null, stderr: null };
  const { handle, error } = start(n.file, n.args, opts);

  if(error) {
    result.error = error;
    return result;
  }

  result.pid = handle.pid;
  const fds = handle.stdio ?? [];
  const input = inputBytes(opts.input);
  const maxBuffer = opts.maxBuffer ?? kMaxBuffer;
  const deadline = opts.timeout > 0 ? Date.now() + opts.timeout : 0;
  const chunks = { 1: [], 2: [] },
    sizes = { 1: 0, 2: 0 };
  let inFd = opts.stdio[0] == 'pipe' ? (fds[0] ?? -1) : -1,
    inOff = 0;
  const readFds = [1, 2].filter(i => opts.stdio[i] == 'pipe' && fds[i] >= 0).map(i => ({ i, fd: fds[i] }));

  if(inFd >= 0 && (!input || !input.length)) {
    os.close(inFd);
    inFd = -1;
  }

  const buf = new ArrayBuffer(READ_SIZE);
  let killed = false;

  while(readFds.length || inFd >= 0) {
    const pfds = readFds.map(r => ({ fd: r.fd, events: POLLIN, revents: 0 }));
    if(inFd >= 0) pfds.push({ fd: inFd, events: POLLOUT, revents: 0 });
    const wait = deadline ? Math.max(0, deadline - Date.now()) : -1;

    if(poll(pfds, pfds.length, wait) == 0) {
      result.error = errnoError('ETIMEDOUT', -110, `spawnSync ${n.file}`, n.file, n.args);
      handle.kill(signum(String(opts.killSignal ?? 'SIGTERM')));
      killed = true;
      break;
    }

    for(let k = 0; k < pfds.length; k++) {
      if(!pfds[k].revents) continue;
      if(k < readFds.length) {
        const { i, fd } = readFds[k];
        const r = os.read(fd, buf, 0, READ_SIZE);

        if(r > 0) {
          chunks[i].push(Buffer.from(buf.slice(0, r)));
          sizes[i] += r;
          if(sizes[i] > maxBuffer && !killed) {
            result.error = errnoError('ENOBUFS', -105, `spawnSync ${n.file}`, n.file, n.args);
            handle.kill(signum(String(opts.killSignal ?? 'SIGTERM')));
            killed = true;
          }
        } else {
          os.close(fd);
          readFds.splice(k, 1);
          break;
        }
      } else {
        const len = Math.min(4096, input.length - inOff);
        const w = os.write(inFd, input.buffer, input.byteOffset + inOff, len);
        inOff += w > 0 ? w : input.length;
        if(inOff >= input.length) {
          os.close(inFd);
          inFd = -1;
        }
      }
    }

    if(killed) break;
  }

  for(const r of readFds) os.close(r.fd);
  if(inFd >= 0) os.close(inFd);

  const st = handle.wait() ?? {};
  result.status = st.exitCode ?? null;
  result.signal = st.signalCode ?? null;

  const enc = toEncoding(opts.encoding);
  const out = i => (opts.stdio[i] == 'pipe' ? (enc ? Buffer.concat(chunks[i]).toString(enc) : Buffer.concat(chunks[i])) : null);
  result.stdout = out(1);
  result.stderr = out(2);
  result.output = [null, result.stdout, result.stderr];
  return result;
}

function checkExecSync(r, cmd, opts) {
  if(r.error) throw Object.assign(r.error, { status: r.status, signal: r.signal, output: r.output, pid: r.pid, stdout: r.stdout, stderr: r.stderr });
  if(r.status !== 0 || r.signal) {
    const stderr = r.stderr ? r.stderr.toString() : '';
    const e = new Error(`Command failed: ${cmd}${stderr ? `\n${stderr}` : ''}`);
    throw Object.assign(e, { status: r.status, signal: r.signal, output: r.output, pid: r.pid, stdout: r.stdout, stderr: r.stderr });
  }

  return r.stdout;
}

/* exec*Sync: stderr goes to the parent's stderr unless stdio is given */
const syncOptions = options => ({ ...options, stdio: options?.stdio ?? ['pipe', 'pipe', 'inherit'] });

export function execFileSync(file, args, options) {
  if(args && !Array.isArray(args) && typeof args == 'object') [options, args] = [args, undefined];
  const opts = syncOptions(options);
  const r = spawnSync(file, args, opts);
  return checkExecSync(r, [file, ...(args ?? [])].join(' '), opts);
}

export function execSync(command, options) {
  if(typeof command != 'string') throw argType('command', 'of type string', command);
  const opts = syncOptions(options);
  const r = spawnSync(command, [], { ...opts, shell: opts.shell ?? true });
  return checkExecSync(r, command, opts);
}

export const kill = native.kill;

export default { ChildProcess, spawn, spawnSync, exec, execSync, execFile, execFileSync, fork, kill };
