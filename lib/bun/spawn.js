/* bun/spawn.js: Bun.spawn() and Bun.spawnSync().
 * depends on: node/child_process (the native child_process engine: spawn,
 * waitpid, pipes), stream (WHATWG ReadableStream), os (write handlers),
 * misc (signum), fs, path, node/buffer.
 * rule: Bun's names, argument shapes and results, checked against bun 1.4.2.
 *
 *   const p = Bun.spawn(['ls', '-l'], { cwd, env, stdin, stdout, stderr, timeout, killSignal, signal, onExit });
 *   await new Response(p.stdout).text(), await p.exited
 *   Bun.spawnSync(['ls']).stdout.toString()
 *
 * | Bun                        | here                                        |
 * | -------------------------- | ------------------------------------------- |
 * | stdout, stderr 'pipe'      | a ReadableStream of Uint8Array chunks, read |
 * |                            | while the stream wants data (backpressure)  |
 * | stdin 'pipe'               | a FileSink: write(), flush(), end(), queued |
 * |                            | and written when the pipe has room          |
 * | stdin Blob, Response,      | a pipe fed from it, then closed             |
 * |   ReadableStream, bytes    |                                             |
 * | stdio BunFile, fd number   | the file opened (or the fd) for the child   |
 * | exited                     | the exit code, 128 + signal when signaled   |
 *
 * limits:
 *   resourceUsage(), ipc, terminal, ref()/unref() are absent or do nothing
 *   stream .text() .json() .bytes() .arrayBuffer() .blob() are on the stream
 *   object, Bun puts them on every ReadableStream
 */
import { spawn as spawnChild, spawnSync as spawnChildSync } from 'node/child_process';
import { ReadableStream } from 'stream';
import { Buffer } from 'node/buffer';
import { signum } from 'misc';
import * as os from 'os';
import { accessSync, closeSync, constants, existsSync, openSync } from 'fs';
import { delimiter, isAbsolute, join, resolve } from 'path';

const PIPE_BUF = 4096;

const invalidArg = message => Object.assign(new TypeError(message), { code: 'ERR_INVALID_ARG_TYPE' });
const noEntry = message => Object.assign(new Error(message), { code: 'ENOENT', errno: -2 });

const isBytes = v => v instanceof ArrayBuffer || ArrayBuffer.isView(v);
const isStream = v => v !== null && typeof v == 'object' && typeof v.getReader == 'function';
const isFile = v => v !== null && typeof v == 'object' && typeof v.name == 'string' && typeof v.stream == 'function' && v.constructor?.name == 'BunFile';

const bytesOf = v => (typeof v == 'string' ? new TextEncoder().encode(v) : v instanceof ArrayBuffer ? new Uint8Array(v) : new Uint8Array(v.buffer, v.byteOffset, v.byteLength));

const exitCodeOf = (code, signal) => (signal ? 128 + (typeof signal == 'number' ? signal : signum(String(signal))) : code);

/* ---- arguments ---- */

/* spawn(cmd, options) or spawn({ cmd, ...options }) as { cmd, options } */
function parseArguments(argument, options) {
  if(argument !== null && typeof argument == 'object' && !Array.isArray(argument)) [argument, options] = [argument.cmd, argument];

  if(!Array.isArray(argument)) throw invalidArg('cmd must be an array');
  if(argument.length == 0) throw invalidArg('cmd must not be empty');

  return { cmd: argument.map(String), options: options ?? {} };
}

/* the executable of cmd[0], as Bun fails when there is none: throws ENOENT */
function checkExecutable(cmd, options) {
  const name = cmd[0];
  const cwd = options.cwd;

  if(cwd !== undefined && !existsSync(cwd)) throw Object.assign(noEntry(`ENOENT: no such file or directory, posix_spawn '${name}'`), { syscall: 'posix_spawn', path: name });

  const path = (options.env ?? globalThis.process?.env ?? {}).PATH ?? '/usr/local/bin:/usr/bin:/bin';
  const dirs = name.includes('/') ? [''] : path.split(delimiter);

  for(const dir of dirs) {
    try {
      accessSync(resolve(cwd ?? '.', dir ? join(dir, name) : name), constants.X_OK);
      return;
    } catch(e) {}
  }

  throw noEntry(`Executable not found in $PATH: "${name}"`);
}

/* the stdio entries of the options, [stdin, stdout, stderr] */
function stdioOf(options, defaults) {
  const list = Array.isArray(options.stdio) ? options.stdio : [options.stdin, options.stdout, options.stderr];

  return defaults.map((fallback, i) => (list[i] === undefined || list[i] === null ? fallback : list[i]));
}

/* one entry as { mode, fd?, input? }: what the child gets, and what is fed to it */
function plan(value, i, opened) {
  if(value === 'pipe' || value === 'ignore' || value === 'inherit') return { mode: value };
  if(typeof value == 'number') return { mode: value };

  if(i == 0 && (isBytes(value) || typeof value == 'string' || isStream(value) || (value && typeof value.arrayBuffer == 'function' && !isFile(value)))) return { mode: 'pipe', input: value };

  if(isFile(value)) {
    const fd = openSync(value.name, i == 0 ? 'r' : 'w');

    opened.push(fd);
    return { mode: fd };
  }

  throw invalidArg(`stdio ${['stdin', 'stdout', 'stderr'][i]} has an unsupported value`);
}

/* ---- FileSink: the stdin of a Subprocess ---- */

class FileSink {
  #fd;
  #queue = [];
  #offset = 0;
  #ending = false;
  #closed = false;
  #waiting = [];

  constructor(fd) {
    this.#fd = fd;
  }

  /* queues chunk, returns its length in bytes; written when the pipe has room */
  write(chunk) {
    if(this.#closed || this.#ending) return 0;

    const bytes = bytesOf(chunk).slice();

    if(bytes.length) {
      this.#queue.push(bytes);
      os.setWriteHandler(this.#fd, () => this.#drain());
    }

    return bytes.length;
  }

  #drain() {
    const head = this.#queue[0];

    if(head === undefined) return this.#settle();

    const n = os.write(this.#fd, head.buffer, head.byteOffset + this.#offset, Math.min(PIPE_BUF, head.length - this.#offset));

    if(n < 0) {
      /* the reader is gone */
      this.#queue.length = 0;
      return this.#settle();
    }

    this.#offset += n;

    if(this.#offset >= head.length) {
      this.#queue.shift();
      this.#offset = 0;
    }

    if(!this.#queue.length) this.#settle();
  }

  /* the queue is empty: stop watching, close when ending, wake flush() */
  #settle() {
    os.setWriteHandler(this.#fd, null);

    if(this.#ending && !this.#closed) {
      this.#closed = true;
      os.close(this.#fd);
    }

    for(const resolve of this.#waiting.splice(0)) resolve(0);
  }

  flush() {
    if(!this.#queue.length) return 0;

    return new Promise(resolve => this.#waiting.push(resolve));
  }

  end() {
    if(this.#ending) return 0;

    this.#ending = true;

    if(!this.#queue.length) this.#settle();

    return 0;
  }

  ref() {}
  unref() {}
}

/* feeds input into sink, then ends it */
async function feed(sink, input) {
  try {
    if(isBytes(input) || typeof input == 'string') sink.write(input);
    else if(isStream(input)) {
      const reader = input.getReader();

      for(;;) {
        const { value, done } = await reader.read();

        if(done) break;

        sink.write(value);
      }
    } else sink.write(await input.arrayBuffer());
  } finally {
    sink.end();
  }
}

/* ---- the output streams ---- */

/* a ReadableStream of the chunks of a child pipe, pausing the pipe while the queue is full */
function outputStream(pipe) {
  const stream = new ReadableStream({
    start(controller) {
      pipe.on('data', chunk => {
        const bytes = typeof chunk == 'string' ? new TextEncoder().encode(chunk) : new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);

        controller.enqueue(bytes);
        if(controller.desiredSize <= 0) pipe.pause();
      });
      pipe.on('end', () => controller.close());
      pipe.on('error', error => controller.error(error));
    },
    pull() {
      pipe.resume();
    },
    cancel() {
      pipe.destroy();
    },
  });

  const read = name => () => globalThis.Bun[name](stream);

  for(const [method, helper] of [['text', 'readableStreamToText'], ['json', 'readableStreamToJSON'], ['bytes', 'readableStreamToBytes'], ['arrayBuffer', 'readableStreamToArrayBuffer'], ['blob', 'readableStreamToBlob']])
    Object.defineProperty(stream, method, { value: read(helper), writable: true, configurable: true });

  return stream;
}

/* ---- Subprocess ---- */

class Subprocess {
  #child;
  #killed = false;
  #exited;
  #stdin;
  #stdout;
  #stderr;

  constructor(child, stdin, stdout, stderr, exited) {
    this.#child = child;
    this.#stdin = stdin;
    this.#stdout = stdout;
    this.#stderr = stderr;
    this.#exited = exited;
  }

  get pid() {
    return this.#child.pid;
  }

  get stdin() {
    return this.#stdin;
  }

  get stdout() {
    return this.#stdout;
  }

  get stderr() {
    return this.#stderr;
  }

  get readable() {
    return this.#stdout;
  }

  get writable() {
    return this.#stdin;
  }

  get stdio() {
    return [this.#stdin ?? null, this.#stdout ?? null, this.#stderr ?? null];
  }

  get exited() {
    return this.#exited;
  }

  get exitCode() {
    return this.#child.exitCode ?? null;
  }

  get signalCode() {
    return this.#child.signalCode ?? null;
  }

  /* Bun 1.4.2 reports true after a normal exit too */
  get killed() {
    return this.#killed || this.exitCode !== null || this.signalCode !== null;
  }

  kill(signal = 'SIGTERM') {
    this.#killed = true;
    this.#child.kill(typeof signal == 'number' ? signal : String(signal));
  }

  ref() {}
  unref() {}
}

export function spawn(argument, options) {
  const { cmd, options: o } = parseArguments(argument, options);

  checkExecutable(cmd, o);

  const opened = [];
  const plans = stdioOf(o, ['ignore', 'pipe', 'inherit']).map((value, i) => plan(value, i, opened));

  const child = spawnChild(cmd[0], cmd.slice(1), {
    cwd: o.cwd,
    env: o.env,
    stdio: plans.map(p => p.mode),
    timeout: o.timeout,
    killSignal: o.killSignal,
  });

  const stdin = plans[0].mode == 'pipe' ? new FileSink(child.stdin.fd) : undefined;
  const stdout = plans[1].mode == 'pipe' ? outputStream(child.stdout) : undefined;
  const stderr = plans[2].mode == 'pipe' ? outputStream(child.stderr) : undefined;
  let subprocess;

  const exited = new Promise(resolve => {
    child.on('exit', (code, signal) => {
      for(const fd of opened) closeSync(fd);

      resolve(exitCodeOf(code, signal));
      o.onExit?.(subprocess, code ?? null, signal ?? null, null);
    });
    child.on('error', () => resolve(-2));
  });

  subprocess = new Subprocess(child, stdin, stdout, stderr, exited);

  if(plans[0].input !== undefined) feed(stdin, plans[0].input).catch(() => {});

  if(o.signal) {
    if(o.signal.aborted) subprocess.kill(o.killSignal);
    else o.signal.addEventListener('abort', () => subprocess.kill(o.killSignal), { once: true });
  }

  return subprocess;
}

export function spawnSync(argument, options) {
  const { cmd, options: o } = parseArguments(argument, options);

  checkExecutable(cmd, o);

  const opened = [];
  const [stdin, stdout, stderr] = stdioOf(o, ['ignore', 'pipe', 'pipe']).map((value, i) => (i == 0 && (isBytes(value) || typeof value == 'string') ? { mode: 'pipe', input: value } : plan(value, i, opened)));

  const r = spawnChildSync(cmd[0], cmd.slice(1), {
    cwd: o.cwd,
    env: o.env,
    stdio: [stdin.mode, stdout.mode, stderr.mode],
    input: stdin.input === undefined ? undefined : bytesOf(stdin.input),
    timeout: o.timeout,
    killSignal: o.killSignal,
    maxBuffer: o.maxBuffer,
  });

  for(const fd of opened) closeSync(fd);

  const result = { exitCode: r.status, stdout: stdout.mode == 'pipe' ? Buffer.from(r.stdout) : undefined, stderr: stderr.mode == 'pipe' ? Buffer.from(r.stderr) : undefined, success: r.status === 0 };

  if(r.signal) result.signalCode = r.signal;

  result.pid = r.pid;

  return result;
}

export default { spawn, spawnSync };
