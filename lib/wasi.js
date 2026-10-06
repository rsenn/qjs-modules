/* wasi.js: WASI, Node's `node:wasi` class (WASI preview1) over fs and process.
 * runs on qjsm, node, bun and deno (fs, process, Atomics; crypto if present).
 * rule: guest paths are resolved inside a preopened directory, never above it.
 *
 * ```js
 * import { WASI } from 'wasi';
 * import * as wasm from 'wasm';   // qjsm; elsewhere use the WebAssembly global
 *
 * const wasi = new WASI({ version: 'preview1', args: ['prog', 'x'], env: { A: '1' }, preopens: { '/sandbox': '.' } });
 * const instance = new wasm.Instance(new wasm.Module(bytes), wasi.getImportObject());
 * const code = wasi.start(instance); // runs _start; the exit code
 * ```
 */
import * as fs from 'fs';

const process = globalThis.process;

/* WASI errno values */
export const errno = {
  SUCCESS: 0, E2BIG: 1, ACCES: 2, AGAIN: 6, BADF: 8, BUSY: 10, EXIST: 20, FAULT: 21, INTR: 27, INVAL: 28, IO: 29,
  ISDIR: 31, LOOP: 32, MFILE: 33, NAMETOOLONG: 37, NOENT: 44, NOMEM: 48, NOSPC: 51, NOSYS: 52, NOTDIR: 54,
  NOTEMPTY: 55, NOTSUP: 58, NXIO: 60, OVERFLOW: 61, PERM: 63, PIPE: 64, RANGE: 68, ROFS: 69, SPIPE: 70, XDEV: 75,
  NOTCAPABLE: 76,
};

/* Node error `code` -> WASI errno; anything not listed is IO */
const CODE_ERRNO = {
  EPERM: 63, ENOENT: 44, ESRCH: 71, EINTR: 27, EIO: 29, ENXIO: 60, E2BIG: 1, ENOEXEC: 45, EBADF: 8, ECHILD: 12,
  EAGAIN: 6, ENOMEM: 48, EACCES: 2, EFAULT: 21, EBUSY: 10, EEXIST: 20, EXDEV: 75, ENODEV: 43, ENOTDIR: 54,
  EISDIR: 31, EINVAL: 28, ENFILE: 41, EMFILE: 33, ENOTTY: 59, ETXTBSY: 74, EFBIG: 22, ENOSPC: 51, ESPIPE: 70,
  EROFS: 69, EMLINK: 34, EPIPE: 64, EDOM: 18, ERANGE: 68, EDEADLK: 16, ENAMETOOLONG: 37, ENOLCK: 46, ENOSYS: 52,
  ENOTEMPTY: 55, ELOOP: 32, EOVERFLOW: 61, ENOTSUP: 58, EOPNOTSUPP: 58, ETIMEDOUT: 73, ECONNREFUSED: 14,
};

const FILETYPE = { UNKNOWN: 0, BLOCK_DEVICE: 1, CHARACTER_DEVICE: 2, DIRECTORY: 3, REGULAR_FILE: 4, SOCKET_STREAM: 6, SYMBOLIC_LINK: 7 };
const RIGHTS_ALL = 0x1fffffffn;
const RIGHT_FD_READ = 2n, RIGHT_FD_WRITE = 64n;

/* st_mode & S_IFMT -> filetype */
const MODE_TYPE = { 0o100000: 4, 0o040000: 3, 0o120000: 7, 0o020000: 2, 0o060000: 1, 0o140000: 6 };
const modeType = mode => MODE_TYPE[mode & 0o170000] ?? FILETYPE.UNKNOWN;

const kExit = Symbol('wasi.exit');

class WASIExit {
  constructor(code) {
    this.code = code;
  }
}

/* an error carrying a Node-style `code` */
function codeError(Type, code, message) {
  const e = new Type(message);

  e.code = code;
  return e;
}

/* the WASI errno of a thrown host error */
const toErrno = e => CODE_ERRNO[e?.code] ?? errno.IO;

/* runs `fn`; returns [0, result] or [WASI errno] */
function attempt(fn) {
  try {
    return [errno.SUCCESS, fn()];
  } catch(e) {
    return [toErrno(e)];
  }
}

/* runs `fn` for its effect; returns 0 or a WASI errno */
const tryHost = fn => attempt(fn)[0];

/* UTF-8 of a JS string and back; TextEncoder/TextDecoder where qjsm lacks them */
const encode = str => (typeof TextEncoder == 'function' ? new TextEncoder().encode(str) : Uint8Array.from(unescape(encodeURIComponent(str)), c => c.charCodeAt(0)));
const decode = u8 => (typeof TextDecoder == 'function' ? new TextDecoder().decode(u8) : decodeURIComponent(escape(String.fromCharCode(...u8))));

/* blocks for `ms`; Atomics.wait where the engine allows it, else a spin */
function sleep(ms) {
  try {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
  } catch(e) {
    for(const end = Date.now() + ms; Date.now() < end; );
  }
}

/* fills the guest buffer with random bytes */
function randomFill(u8) {
  if(typeof globalThis.crypto?.getRandomValues == 'function') {
    for(let i = 0; i < u8.length; i += 65536) crypto.getRandomValues(u8.subarray(i, i + 65536));
    return;
  }

  const fd = fs.openSync('/dev/urandom', fs.constants.O_RDONLY);

  try {
    for(let at = 0; at < u8.length; ) at += fs.readSync(fd, u8, at, u8.length - at);
  } finally {
    fs.closeSync(fd);
  }
}

const nsFromMs = ms => BigInt(Math.floor(ms)) * 1000000n + BigInt(Math.round((ms % 1) * 1000000));

/* a guest path inside directory `base` (a host path), or null if it escapes.
 *
 *   'a/../b'  -> base/b        '..'     -> null        '/x'  -> null
 */
function inside(base, rel) {
  if(rel.startsWith('/')) return null;

  const parts = [];

  for(const part of rel.split('/')) {
    if(part == '' || part == '.') continue;
    if(part == '..') {
      if(!parts.length) return null;
      parts.pop();
    } else parts.push(part);
  }

  return parts.length ? base.replace(/\/+$/, '') + '/' + parts.join('/') : base;
}

/* WASI: the guest-facing system interface.
 *
 * ```js
 * new WASI({ version, args, env, preopens, returnOnExit, stdin, stdout, stderr })
 * wasi.getImportObject()        // { wasi_snapshot_preview1: wasiImport }
 * wasi.start(instance)          // runs a command (_start), returns the exit code
 * wasi.initialize(instance)     // runs a reactor (_initialize)
 * wasi.finalizeBindings(instance, { memory })
 * ```
 *
 *   options.version       'preview1' (default) or 'unstable' (import module wasi_unstable)
 *   options.args          argv for the guest, default []
 *   options.env           environment object, default {} (not inherited)
 *   options.preopens      { guestPath: hostPath } directories the guest may open
 *   options.returnOnExit  start() returns the exit code (default true); false exits the process
 *   options.stdin/stdout/stderr  host fds for guest fds 0-2, default 0, 1, 2
 *
 * throws TypeError for a bad option; Error (code ERR_WASI_ALREADY_STARTED) on a second start.
 */
export class WASI {
  constructor(options = {}) {
    const { version = 'preview1', args = [], env = {}, preopens = {}, returnOnExit = true, stdin = 0, stdout = 1, stderr = 2 } = options;

    if(version != 'preview1' && version != 'unstable') throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The property 'options.version' must be 'unstable' or 'preview1'. Received '${version}'`);
    if(!Array.isArray(args)) throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "options.args" property must be an instance of Array');
    if(env === null || typeof env != 'object') throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "options.env" property must be of type object');

    this[kExit] = returnOnExit;
    this._version = version;
    this._args = args.map(String);
    this._env = Object.entries(env).map(([k, v]) => `${k}=${v}`);
    this._fds = new Map([[0, { type: 'stdio', host: stdin }], [1, { type: 'stdio', host: stdout }], [2, { type: 'stdio', host: stderr }]]);
    this._next = 3;
    this._memory = null;
    this._started = false;

    for(const [guest, host] of Object.entries(preopens)) {
      const real = attempt(() => fs.realpathSync(String(host)))[1] ?? String(host);

      this._fds.set(this._next++, { type: 'dir', path: real, root: real, vpath: guest });
    }

    this.wasiImport = this._makeImports();
  }

  getImportObject() {
    return { [this._version == 'unstable' ? 'wasi_unstable' : 'wasi_snapshot_preview1']: this.wasiImport };
  }

  /* runs `instance`'s _start; returns the exit code (proc_exit's, or 0) */
  start(instance) {
    this._bind(instance, 'start');
    if(typeof instance.exports._start != 'function') throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "instance.exports._start" property must be of type function');
    if(instance.exports._initialize !== undefined) throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "instance.exports._initialize" property must be undefined');

    try {
      instance.exports._start();
    } catch(e) {
      if(!(e instanceof WASIExit)) throw e;
      return this._exit(e.code);
    }

    return this._exit(0);
  }

  /* runs `instance`'s optional _initialize (a reactor) */
  initialize(instance) {
    this._bind(instance, 'initialize');
    if(instance.exports._start !== undefined) throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "instance.exports._start" property must be undefined');
    if(typeof instance.exports._initialize == 'function') instance.exports._initialize();
  }

  /* binds the guest memory without running anything (`memory` overrides exports.memory) */
  finalizeBindings(instance, { memory } = {}) {
    if(this._started) throw codeError(Error, 'ERR_WASI_ALREADY_STARTED', 'WASI instance has already started');

    this._started = true;
    this._memory = memory ?? instance?.exports?.memory;
    if(!this._memory || !('buffer' in this._memory)) throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "instance.exports.memory" property must be an instance of WebAssembly.Memory');
  }

  _bind(instance, what) {
    if(!instance || typeof instance != 'object' || !instance.exports) throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "instance" argument must be an instance of WebAssembly.Instance');
    this.finalizeBindings(instance);
  }

  _exit(code) {
    if(this[kExit]) return code;
    process.exit(code);
  }

  /* ---- memory helpers ---- */

  _view() {
    return new DataView(this._memory.buffer);
  }
  _str(ptr, len) {
    return decode(new Uint8Array(this._memory.buffer, ptr, len));
  }

  /* writes a NUL-terminated list of strings: pointers at `ptrs`, bytes at `buf` */
  _putStrings(list, ptrs, buf) {
    const v = this._view(), u8 = new Uint8Array(this._memory.buffer);
    let at = buf;

    list.forEach((s, i) => {
      const bytes = encode(s);

      v.setUint32(ptrs + i * 4, at, true);
      u8.set(bytes, at);
      u8[at + bytes.length] = 0;
      at += bytes.length + 1;
    });
  }

  /* the byte size of `list` as NUL-terminated strings */
  _stringsSize(list) {
    return list.reduce((n, s) => n + encode(s).length + 1, 0);
  }

  /* ---- fd table ---- */

  /* the table entry of `fd`, or undefined */
  _fd(fd) {
    return this._fds.get(fd);
  }

  /* host path of `rel` under directory fd `dir`; returns [errno, path] */
  _path(dir, ptr, len) {
    const d = this._fd(dir);

    if(!d) return [errno.BADF];
    if(d.type != 'dir') return [errno.NOTDIR];

    const real = inside(d.path, this._str(ptr, len));

    if(real === null) return [errno.NOTCAPABLE];
    if(!this._within(d.root, real)) return [errno.NOTCAPABLE];

    return [errno.SUCCESS, real, d];
  }

  /* false when the existing part of `path` resolves outside `root` through a symlink */
  _within(root, path) {
    let p = path;

    for(;;) {
      const [err, real] = attempt(() => fs.realpathSync(p));

      if(!err) return real == root || real.startsWith(root.replace(/\/+$/, '') + '/');
      if(p == root || !p.includes('/')) return true;
      p = p.slice(0, p.lastIndexOf('/')) || '/';
    }
  }

  _stat(path, follow = true) {
    return attempt(() => (follow ? fs.statSync(path) : fs.lstatSync(path)));
  }

  _writeFilestat(ptr, st) {
    const v = this._view();

    v.setBigUint64(ptr, BigInt(st.dev), true);
    v.setBigUint64(ptr + 8, BigInt(st.ino), true);
    v.setUint8(ptr + 16, modeType(st.mode));
    v.setBigUint64(ptr + 24, BigInt(st.nlink), true);
    v.setBigUint64(ptr + 32, BigInt(st.size), true);
    v.setBigUint64(ptr + 40, nsFromMs(st.atimeMs), true);
    v.setBigUint64(ptr + 48, nsFromMs(st.mtimeMs), true);
    v.setBigUint64(ptr + 56, nsFromMs(st.ctimeMs), true);
  }

  /* [errno, stat] of an open fd entry */
  _entryStat(e) {
    if(e.type == 'dir') return this._stat(e.path);
    return attempt(() => fs.fstatSync(e.host));
  }

  /* applies fstflags (ATIM 1, ATIM_NOW 2, MTIM 4, MTIM_NOW 8) to `path` */
  _setTimes(path, atim, mtim, flags) {
    const [e, st] = this._stat(path);
    const now = Date.now();

    if(e) return e;

    const at = flags & 2 ? now : flags & 1 ? Number(atim / 1000000n) : st.atimeMs;
    const mt = flags & 8 ? now : flags & 4 ? Number(mtim / 1000000n) : st.mtimeMs;

    return tryHost(() => fs.utimesSync(path, at / 1000, mt / 1000));
  }

  /* reads or writes the iovecs of fd entry `e` at its position (or `pos` if given) */
  _rw(e, iov, iovcnt, pnum, write, pos) {
    if(!e || e.type == 'dir') return e ? errno.ISDIR : errno.BADF;

    const v = this._view();
    const op = write ? fs.writeSync : fs.readSync;
    const seekable = e.type == 'file';
    let at = pos !== undefined ? Number(pos) : seekable ? e.pos : null;
    let total = 0;

    if(write && e.append && pos === undefined) {
      const [err, st] = attempt(() => fs.fstatSync(e.host));

      if(err) return err;
      at = st.size;
    }

    for(let i = 0; i < iovcnt; i++) {
      const base = v.getUint32(iov + i * 8, true), len = v.getUint32(iov + i * 8 + 4, true);

      if(!len) continue;

      const [err, n] = attempt(() => op(e.host, new Uint8Array(this._memory.buffer, base, len), 0, len, at));

      if(err) {
        if(!total) return err;
        break;
      }

      total += n;
      if(at !== null) at += n;
      if(n < len) break;
    }

    if(pos === undefined && seekable) e.pos = at;
    this._view().setUint32(pnum, total, true);
    return errno.SUCCESS;
  }

  /* ---- the preview1 functions ---- */

  _makeImports() {
    const w = this;
    const v = () => this._view();

    /* wraps each function so a bug in the host layer surfaces as EIO, not a JS exception */
    const imports = {
      args_get: (argv, buf) => (w._putStrings(w._args, argv, buf), 0),
      args_sizes_get(argc, size) {
        v().setUint32(argc, w._args.length, true);
        v().setUint32(size, w._stringsSize(w._args), true);
        return 0;
      },
      environ_get: (env, buf) => (w._putStrings(w._env, env, buf), 0),
      environ_sizes_get(count, size) {
        v().setUint32(count, w._env.length, true);
        v().setUint32(size, w._stringsSize(w._env), true);
        return 0;
      },
      clock_res_get(id, ptr) {
        if(id < 0 || id > 3) return errno.INVAL;
        v().setBigUint64(ptr, 1000n, true);
        return 0;
      },
      clock_time_get(id, precision, ptr) {
        if(id < 0 || id > 3) return errno.INVAL;
        if(id == 0) v().setBigUint64(ptr, nsFromMs(Date.now()), true);
        else {
          v().setBigUint64(ptr, process.hrtime.bigint(), true);
        }
        return 0;
      },
      fd_advise: fd => (w._fd(fd) ? 0 : errno.BADF),
      fd_allocate(fd, offset, len) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;

        const [err, st] = w._entryStat(e);

        if(err) return err;
        return Number(offset + len) > st.size ? tryHost(() => fs.ftruncateSync(e.host, Number(offset + len))) : 0;
      },
      fd_close(fd) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;
        if(e.type == 'file') fs.closeSync(e.host);
        w._fds.delete(fd);
        return 0;
      },
      fd_datasync: fd => (w._fd(fd)?.type == 'file' ? tryHost(() => fs.fdatasyncSync(w._fd(fd).host)) : w._fd(fd) ? 0 : errno.BADF),
      fd_fdstat_get(fd, ptr) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;

        const [err, st] = e.type == 'stdio' ? [0, { mode: 0o020000 }] : w._entryStat(e);
        const view = v();

        if(err) return err;
        view.setUint8(ptr, modeType(st.mode));
        view.setUint16(ptr + 2, e.append ? 1 : 0, true);
        view.setBigUint64(ptr + 8, RIGHTS_ALL, true);
        view.setBigUint64(ptr + 16, RIGHTS_ALL, true);
        return 0;
      },
      fd_fdstat_set_flags(fd, flags) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;
        e.append = !!(flags & 1);
        return 0;
      },
      fd_fdstat_set_rights: fd => (w._fd(fd) ? errno.NOSYS : errno.BADF),
      fd_filestat_get(fd, ptr) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;

        const [err, st] = e.type == 'stdio' ? [0, { dev: 0, ino: 0, mode: 0o020000, nlink: 1, size: 0, atime: 0, mtime: 0, ctime: 0 }] : w._entryStat(e);

        if(err) return err;
        w._writeFilestat(ptr, st);
        return 0;
      },
      fd_filestat_set_size(fd, size) {
        const e = w._fd(fd);

        return !e ? errno.BADF : e.type == 'file' ? tryHost(() => fs.ftruncateSync(e.host, Number(size))) : errno.BADF;
      },
      fd_filestat_set_times(fd, atim, mtim, flags) {
        const e = w._fd(fd);

        return !e || e.type == 'stdio' ? errno.BADF : w._setTimes(e.path, atim, mtim, flags);
      },
      fd_pread: (fd, iov, n, offset, pnum) => w._rw(w._fd(fd), iov, n, pnum, false, offset),
      fd_prestat_get(fd, ptr) {
        const e = w._fd(fd);

        if(!e || e.type != 'dir' || e.vpath === undefined) return errno.BADF;
        v().setUint8(ptr, 0);
        v().setUint32(ptr + 4, encode(e.vpath).length, true);
        return 0;
      },
      fd_prestat_dir_name(fd, ptr, len) {
        const e = w._fd(fd);

        if(!e || e.type != 'dir' || e.vpath === undefined) return errno.BADF;

        const bytes = encode(e.vpath);

        if(len < bytes.length) return errno.INVAL;
        new Uint8Array(w._memory.buffer).set(bytes, ptr);
        return 0;
      },
      fd_pwrite: (fd, iov, n, offset, pnum) => w._rw(w._fd(fd), iov, n, pnum, true, offset),
      fd_read: (fd, iov, n, pnum) => w._rw(w._fd(fd), iov, n, pnum, false),
      fd_readdir(fd, buf, buflen, cookie, pused) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;
        if(e.type != 'dir') return errno.NOTDIR;

        const [err, names] = attempt(() => fs.readdirSync(e.path));

        if(err) return err;

        const view = v(), u8 = new Uint8Array(w._memory.buffer);
        let used = 0;

        for(let i = Number(cookie); i < names.length && used < buflen; i++) {
          const name = encode(names[i]);
          const [, st] = w._stat(e.path + '/' + names[i], false);
          const head = new DataView(new ArrayBuffer(24));

          head.setBigUint64(0, BigInt(i + 1), true);
          head.setBigUint64(8, BigInt(st?.ino ?? 0), true);
          head.setUint32(16, name.length, true);
          head.setUint8(20, st ? modeType(st.mode) : 0);

          for(const chunk of [new Uint8Array(head.buffer), name]) {
            const n = Math.min(chunk.length, buflen - used);

            u8.set(chunk.subarray(0, n), buf + used);
            used += n;
          }
        }

        view.setUint32(pused, used, true);
        return 0;
      },
      fd_renumber(from, to) {
        const e = w._fd(from);

        if(!e || !w._fd(to)) return errno.BADF;
        if(w._fd(to).type == 'file') fs.closeSync(w._fd(to).host);
        w._fds.set(to, e);
        w._fds.delete(from);
        return 0;
      },
      fd_seek(fd, offset, whence, ptr) {
        const e = w._fd(fd);

        if(!e) return errno.BADF;
        if(e.type != 'file') return errno.SPIPE;

        let base = whence == 1 ? e.pos : 0;

        if(whence == 2) {
          const [err, st] = w._entryStat(e);

          if(err) return err;
          base = st.size;
        } else if(whence != 0 && whence != 1) return errno.INVAL;

        const pos = base + Number(offset);

        if(pos < 0) return errno.INVAL;
        e.pos = pos;
        v().setBigUint64(ptr, BigInt(pos), true);
        return 0;
      },
      fd_sync: fd => (w._fd(fd)?.type == 'file' ? tryHost(() => fs.fsyncSync(w._fd(fd).host)) : w._fd(fd) ? 0 : errno.BADF),
      fd_tell(fd, ptr) {
        return imports.fd_seek(fd, 0n, 1, ptr);
      },
      fd_write: (fd, iov, n, pnum) => w._rw(w._fd(fd), iov, n, pnum, true),
      path_create_directory(fd, p, len) {
        const [err, real] = w._path(fd, p, len);

        return err ? err : tryHost(() => fs.mkdirSync(real, 0o777));
      },
      path_filestat_get(fd, flags, p, len, buf) {
        const [err, real] = w._path(fd, p, len);

        if(err) return err;

        const [e, st] = w._stat(real, !!(flags & 1));

        if(e) return e;
        w._writeFilestat(buf, st);
        return 0;
      },
      path_filestat_set_times(fd, flags, p, len, atim, mtim, fst) {
        const [err, real] = w._path(fd, p, len);

        return err ? err : w._setTimes(real, atim, mtim, fst);
      },
      path_link(ofd, oflags, op, olen, nfd, np, nlen) {
        const [e1, from] = w._path(ofd, op, olen), [e2, to] = w._path(nfd, np, nlen);

        return e1 || e2 || tryHost(() => fs.linkSync(from, to));
      },
      path_open(dirfd, dirflags, p, len, oflags, rights, inherit, fdflags, pfd) {
        const [err, real, dir] = w._path(dirfd, p, len);

        if(err) return err;

        const [serr, st] = w._stat(real, !!(dirflags & 1));

        if(!serr && modeType(st.mode) == FILETYPE.SYMBOLIC_LINK) return errno.LOOP;
        if(oflags & 2 && !serr && modeType(st.mode) != FILETYPE.DIRECTORY) return errno.NOTDIR;
        if(oflags & 4 && oflags & 1 && !serr) return errno.EXIST;

        if(!serr && modeType(st.mode) == FILETYPE.DIRECTORY) {
          if(rights & RIGHT_FD_WRITE) return errno.ISDIR;
          w._fds.set(w._next, { type: 'dir', path: real, root: dir.root });
          v().setUint32(pfd, w._next++, true);
          return 0;
        }

        if(oflags & 2) return serr || errno.NOTDIR;

        const read = rights & RIGHT_FD_READ, write = rights & RIGHT_FD_WRITE;
        const c = fs.constants;
        let flags = write ? (read ? c.O_RDWR : c.O_WRONLY) : c.O_RDONLY;

        // append is emulated in _rw: no O_APPEND, so a position is always valid
        if(oflags & 1) flags |= c.O_CREAT;
        if(oflags & 4) flags |= c.O_EXCL;
        if(oflags & 8) flags |= c.O_TRUNC;
        if(fdflags & 4) flags |= c.O_NONBLOCK ?? 0;
        if(fdflags & 18) flags |= c.O_SYNC ?? 0;

        const [oerr, host] = attempt(() => fs.openSync(real, flags, 0o666));

        if(oerr) return oerr;
        w._fds.set(w._next, { type: 'file', host, path: real, append: !!(fdflags & 1), pos: 0 });
        v().setUint32(pfd, w._next++, true);
        return 0;
      },
      path_readlink(fd, p, len, buf, buflen, pused) {
        const [err, real] = w._path(fd, p, len);

        if(err) return err;

        const [e, target] = attempt(() => fs.readlinkSync(real));

        if(e) return e;

        const bytes = encode(target), n = Math.min(bytes.length, buflen);

        new Uint8Array(w._memory.buffer).set(bytes.subarray(0, n), buf);
        v().setUint32(pused, n, true);
        return 0;
      },
      path_remove_directory(fd, p, len) {
        const [err, real] = w._path(fd, p, len);

        if(err) return err;

        const [e, st] = w._stat(real, false);

        if(e) return e;
        return modeType(st.mode) == FILETYPE.DIRECTORY ? tryHost(() => fs.rmdirSync(real)) : errno.NOTDIR;
      },
      path_rename(ofd, op, olen, nfd, np, nlen) {
        const [e1, from] = w._path(ofd, op, olen), [e2, to] = w._path(nfd, np, nlen);

        return e1 || e2 || tryHost(() => fs.renameSync(from, to));
      },
      path_symlink(op, olen, fd, np, nlen) {
        const [err, real] = w._path(fd, np, nlen);

        return err ? err : tryHost(() => fs.symlinkSync(w._str(op, olen), real));
      },
      path_unlink_file(fd, p, len) {
        const [err, real] = w._path(fd, p, len);

        if(err) return err;

        const [e, st] = w._stat(real, false);

        if(e) return e;
        return modeType(st.mode) == FILETYPE.DIRECTORY ? errno.ISDIR : tryHost(() => fs.unlinkSync(real));
      },
      poll_oneoff(inp, out, n, pevents) {
        const view = v();
        let events = 0, wait = null;
        const ready = [];

        /* subscription: userdata u64@0, tag u8@8 (0 clock, 1 fd_read, 2 fd_write), clock: id u32@16 timeout u64@24 flags u16@40 */
        for(let i = 0; i < n; i++) {
          const s = inp + i * 48, tag = view.getUint8(s + 8), user = view.getBigUint64(s, true);

          if(tag == 0) {
            const timeout = view.getBigUint64(s + 24, true), abs = view.getUint16(s + 40, true) & 1;
            const now = BigInt(Date.now()) * 1000000n;
            const ms = Number((abs ? (timeout > now ? timeout - now : 0n) : timeout) / 1000000n);

            if(wait === null || ms < wait.ms) wait = { ms, user, tag };
          } else ready.push({ user, tag });
        }

        if(!ready.length && wait) sleep(wait.ms);

        const fired = ready.length ? ready : wait ? [wait] : [];

        for(const ev of fired) {
          const o = out + events++ * 32;

          view.setBigUint64(o, ev.user, true);
          view.setUint16(o + 8, 0, true);
          view.setUint8(o + 10, ev.tag);
          view.setBigUint64(o + 16, 0n, true);
          view.setUint16(o + 24, 0, true);
        }

        view.setUint32(pevents, events, true);
        return 0;
      },
      proc_exit(code) {
        throw new WASIExit(code);
      },
      proc_raise: () => errno.NOSYS,
      random_get(buf, len) {
        randomFill(new Uint8Array(w._memory.buffer, buf, len));
        return 0;
      },
      sched_yield: () => 0,
      sock_accept: () => errno.NOSYS,
      sock_recv: () => errno.NOSYS,
      sock_send: () => errno.NOSYS,
      sock_shutdown: () => errno.NOSYS,
    };

    /* a host bug must not unwind through the guest: report EIO unless it is a proc_exit */
    for(const [name, fn] of Object.entries(imports))
      imports[name] = (...args) => {
        try {
          return fn(...args);
        } catch(e) {
          if(e instanceof WASIExit) throw e;
          return errno.IO;
        }
      };

    return imports;
  }
}

export default { WASI };
