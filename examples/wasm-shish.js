#!/usr/bin/env qjsm
import * as std from 'std';
import { readFileSync } from 'fs';
import * as wasm from 'wasm';

/**
 * Loads shutil.wasm and shish.wasm (Emscripten builds of the shish shell,
 * from https://github.com/rsenn/shish) through the `wasm` module.
 *
 *   qjsm examples/wasm-shish.js [dir-with-the-wasm-files]
 *
 * Emscripten minifies import and export names, so the tables below map
 * each letter to the name it has in these particular builds.
 */

const dir = scriptArgs[1] ?? `${std.getenv('HOME')}/Sources/rsenn/.cache/pages/shish/assets`;

const ENOSYS = -38;
const utf8 = s => unescape(encodeURIComponent(s));
const unutf8 = s => decodeURIComponent(escape(s));

class ExitStatus {
  constructor(code) {
    this.code = code;
  }
}
class LongJmp {} /* thrown by _emscripten_throw_longjmp, caught by invoke_* */

/* import letters -> Emscripten names (from each build's wasmImports) */
const SHUTIL_IMPORTS = {
  i: '___syscall_fcntl64', h: '___syscall_fstat64', t: '___syscall_newfstatat', p: '___syscall_openat',
  n: '___syscall_unlinkat', k: '__emscripten_throw_longjmp', q: '__msync_js', r: '__munmap_js',
  l: '_emscripten_resize_heap', c: '_exit', j: '_fd_close', s: '_fd_fdstat_get', o: '_fd_read', m: '_fd_write',
  a: 'invoke_ii', b: 'invoke_iii', e: 'invoke_iiii', g: 'invoke_v', d: 'invoke_vi', f: 'invoke_vii'
};
const SHUTIL_EXPORTS = { memory: 'u', table: 'w', ctors: 'v', free: 'x', format: 'z', malloc: 'A', parse: 'B', setThrew: 'D', restore: 'E', current: 'G' };

const SHISH_IMPORTS = {
  G: '___call_sighandler', fa: '___syscall_chdir', ea: '___syscall_chmod', da: '___syscall_dup', ca: '___syscall_dup3',
  ga: '___syscall_faccessat', e: '___syscall_fcntl64', aa: '___syscall_fstat64', Y: '___syscall_getcwd',
  E: '___syscall_getdents64', X: '___syscall_getegid32', W: '___syscall_geteuid32', r: '___syscall_getgid32',
  g: '___syscall_getuid32', q: '___syscall_ioctl', o: '___syscall_linkat', Z: '___syscall_lstat64',
  U: '___syscall_mkdirat', _: '___syscall_newfstatat', M: '___syscall_openat', L: '___syscall_pipe2',
  n: '___syscall_poll', K: '___syscall_poll_nonblocking', D: '___syscall_readlinkat', C: '___syscall_renameat',
  B: '___syscall_rmdir', $: '___syscall_stat64', A: '___syscall_symlinkat', y: '___syscall_umask',
  x: '___syscall_unlinkat', w: '___syscall_utimensat', J: '__abort_js', I: '__emscripten_runtime_keepalive_clear',
  t: '__emscripten_throw_longjmp', R: '__localtime_js', S: '__mktime_js', N: '__mmap_js', O: '__msync_js',
  Q: '__munmap_js', m: '__setitimer_js', ha: '__tzset_js', T: '_clock_time_get', ba: '_emscripten_date_now',
  z: '_emscripten_get_heap_max', h: '_emscripten_get_now', u: '_emscripten_resize_heap', ia: '_environ_get',
  ja: '_environ_sizes_get', f: '_exit', k: '_fd_close', p: '_fd_fdstat_get', F: '_fd_read', V: '_fd_seek',
  v: '_fd_write', i: 'invoke_i', c: 'invoke_ii', b: 'invoke_iii', d: 'invoke_iiii', j: 'invoke_iiiii',
  s: 'invoke_v', a: 'invoke_vi', P: 'invoke_vii', l: 'invoke_viii', H: '_proc_exit'
};
const SHISH_EXPORTS = { memory: 'ka', table: 'na', ctors: 'la', main: 'ma', setThrew: 'qa', restore: 'ra', alloc: 'sa', current: 'ta' };

/* Loads `file`; returns { exports, heap(), host } once instantiated.
 * `letters` maps import letters to names, `names` the export letters. */
function load(file, letters, names) {
  const module = new wasm.Module(readFileSync(`${dir}/${file}`));
  let ex;
  const heap = () => new DataView(ex[names.memory].buffer);
  const u8 = () => new Uint8Array(ex[names.memory].buffer);

  const readString = ptr => {
    const m = u8();
    let s = '';
    while(m[ptr]) s += String.fromCharCode(m[ptr++]);
    return unutf8(s);
  };

  const output = (fd, text) => (fd === 2 ? std.err : std.out).puts(text);

  /* a tiny in-memory file system: enough for tmpfile()-style scratch files */
  const files = new Map(), fds = new Map();
  let nextFd = 3;
  const bytes = (iov, iovcnt) => {
    const v = [];
    for(let i = 0; i < iovcnt; i++) v.push([heap().getUint32(iov + i * 8, true), heap().getUint32(iov + i * 8 + 4, true)]);
    return v;
  };
  const writeStat = (buf, size, mode) => {
    u8().fill(0, buf, buf + 96);
    heap().setUint32(buf + 4, mode, true);
    heap().setUint32(buf + 8, 1, true);
    heap().setBigInt64(buf + 24, BigInt(size), true);
    heap().setUint32(buf + 32, 4096, true);
  };

  const host = {
    _fd_write(fd, iov, iovcnt, pnum) {
      let n = 0;
      for(const [base, len] of bytes(iov, iovcnt)) {
        if(fds.has(fd)) {
          const f = fds.get(fd);
          if(f.pos + len > f.file.data.length) {
            const grown = new Uint8Array(Math.max(f.pos + len, f.file.data.length * 2));
            grown.set(f.file.data);
            f.file.data = grown;
          }
          f.file.data.set(u8().subarray(base, base + len), f.pos);
          f.pos += len;
          f.file.size = Math.max(f.file.size, f.pos);
        } else {
          let s = '';
          for(let j = 0; j < len; j++) s += String.fromCharCode(u8()[base + j]);
          output(fd, unutf8(s));
        }
        n += len;
      }
      heap().setUint32(pnum, n, true);
      return 0;
    },
    _fd_read(fd, iov, iovcnt, pnum) {
      const f = fds.get(fd);
      let n = 0;
      if(!f) return 8; /* EBADF */
      for(const [base, len] of bytes(iov, iovcnt)) {
        const chunk = f.file.data.subarray(f.pos, Math.min(f.pos + len, f.file.size));
        u8().set(chunk, base);
        f.pos += chunk.length;
        n += chunk.length;
        if(chunk.length < len) break;
      }
      heap().setUint32(pnum, n, true);
      return 0;
    },
    _fd_close: fd => (fds.delete(fd), 0),
    ___syscall_openat(dirfd, path, flags) {
      const name = readString(path);
      let file = files.get(name);
      if(!file) {
        if(!(flags & 64)) return -2; /* ENOENT */
        files.set(name, (file = { data: new Uint8Array(256), size: 0 }));
      } else if(flags & 512) file.size = 0;
      fds.set(nextFd, { file, pos: 0 });
      return nextFd++;
    },
    ___syscall_getcwd(buf) {
      u8().set([0x2f, 0], buf);
      return 2;
    },
    ___syscall_umask: () => 18,
    ___syscall_getuid32: () => 1000,
    ___syscall_geteuid32: () => 1000,
    ___syscall_getgid32: () => 1000,
    ___syscall_getegid32: () => 1000,
    ___syscall_pipe2(pfd) {
      const file = { data: new Uint8Array(256), size: 0 };
      fds.set(nextFd, { file, pos: 0 });
      fds.set(nextFd + 1, { file, pos: 0 });
      heap().setInt32(pfd, nextFd, true);
      heap().setInt32(pfd + 4, nextFd + 1, true);
      nextFd += 2;
      return 0;
    },
    ___syscall_fcntl64: (fd, cmd) => (fd < 3 || fds.has(fd) ? (cmd === 3 ? 2 : 0) : -9), /* F_GETFL: O_RDWR; else EBADF */
    ___syscall_unlinkat: (dirfd, path) => (files.delete(readString(path)), 0),
    ___syscall_fstat64(fd, buf) {
      const f = fds.get(fd);
      writeStat(buf, f ? f.file.size : 0, f ? 0o100644 : 0o020620);
      return 0;
    },
    ___syscall_newfstatat(dirfd, path, buf) {
      const file = files.get(readString(path));
      if(!file) return -2;
      writeStat(buf, file.size, 0o100644);
      return 0;
    },
    _exit: code => { throw new ExitStatus(code); },
    _proc_exit: code => { throw new ExitStatus(code); },
    __abort_js: () => { throw new Error('abort()'); },
    __emscripten_throw_longjmp: () => { throw new LongJmp(); },
    _emscripten_date_now: () => Date.now(),
    _emscripten_get_now: () => Date.now(),
    _emscripten_get_heap_max: () => 2147483648,
    _clock_time_get(clk, precision, ptime) {
      heap().setBigUint64(ptime, BigInt(Date.now()) * 1000000n, true);
      return 0;
    },
    _environ_sizes_get(pcount, psize) {
      heap().setUint32(pcount, 0, true);
      heap().setUint32(psize, 0, true);
      return 0;
    },
    _environ_get: () => 0,
    _fd_fdstat_get: () => 0
  };

  /* setjmp/longjmp emulation: a call that may longjmp runs inside invoke_*. */
  const invoke = (index, ...args) => {
    const sp = ex[names.current]();
    try {
      return ex[names.table].get(index)(...args);
    } catch(e) {
      ex[names.restore](sp);
      if(!(e instanceof LongJmp)) throw e;
      ex[names.setThrew](1, 0);
    }
  };

  const a = {};
  for(const [letter, name] of Object.entries(letters))
    a[letter] = name.startsWith('invoke_') ? (...args) => invoke(...args)
      : name in host ? (...args) => host[name](...args)
      : name.startsWith('___syscall_') ? () => ENOSYS
      : () => 0;

  ex = new wasm.Instance(module, { a }).exports;
  ex[names.ctors]();
  return { ex, heap, u8, readString };
}

/* writes a NUL-terminated UTF-8 copy of `s` with `alloc(size)`; returns its address. */
function putString(inst, alloc, s) {
  const bytes = utf8(s);
  const ptr = alloc(bytes.length + 1);
  const m = inst.u8();
  for(let i = 0; i < bytes.length; i++) m[ptr + i] = bytes.charCodeAt(i);
  m[ptr + bytes.length] = 0;
  return ptr;
}

/* ---- shutil.wasm: format and parse shell scripts ---- */

{
  const shutil = load('shutil.wasm', SHUTIL_IMPORTS, SHUTIL_EXPORTS);
  const { ex } = shutil;
  const call = (fn, source, ...rest) => {
    const src = putString(shutil, ex[SHUTIL_EXPORTS.malloc], source);
    const out = ex[fn](src, ...rest);
    ex[SHUTIL_EXPORTS.free](src);
    if(!out) return null;
    const text = shutil.readString(out);
    ex[SHUTIL_EXPORTS.free](out);
    return text;
  };

  const script = 'for f in *.c;do echo "$f";done|sort&&echo   done';

  console.log('--- shutil: format');
  console.log(call(SHUTIL_EXPORTS.format, script));

  console.log('--- shutil: parse (kinds in the AST)');
  const ast = JSON.parse(call(SHUTIL_EXPORTS.parse, script, 0));
  const kinds = new Set();
  (function walk(n) {
    if(Array.isArray(n)) return n.forEach(walk);
    if(n && typeof n === 'object') {
      if(n.kind) kinds.add(n.kind);
      Object.values(n).forEach(walk);
    }
  })(ast);
  console.log([...kinds].join(', '));
}

/* ---- shish.wasm: run a command with the shell ---- */

{
  const shish = load('shish.wasm', SHISH_IMPORTS, SHISH_EXPORTS);
  const { ex } = shish;
  const args = ['shish', '-c', 'echo hello from shish; for i in 1 2 3; do echo "i=$i"; done; echo $((6*7))'];
  const sp = ex[SHISH_EXPORTS.alloc]((args.length + 1) * 4);

  console.log('--- shish: run');
  args.forEach((a, i) => shish.heap().setUint32(sp + i * 4, putString(shish, ex[SHISH_EXPORTS.alloc], a), true));
  shish.heap().setUint32(sp + args.length * 4, 0, true);

  try {
    const status = ex[SHISH_EXPORTS.main](args.length, sp);
    console.log('exit status', status);
  } catch(e) {
    if(!(e instanceof ExitStatus)) throw e;
    console.log('exit status', e.code);
  }
}
