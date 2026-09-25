import { EventEmitter } from 'events';
import * as os from 'os';
import { basename, dirname, extname, join } from 'path';
import { access as sys_access, error as sys_error, fchmod as sys_fchmod, fchown as sys_fchown, fdatasync as sys_fdatasync, fstat as sys_fstat, fsync as sys_fsync, ftruncate as sys_ftruncate, futimes as sys_futimes, IN_ISDIR, IN_ALL_EVENTS, IN_ATTRIB, IN_CLOSE_WRITE, IN_CLOSE_NOWRITE, IN_CLOSE, IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_MODIFY, IN_MOVE_SELF, IN_MOVED_TO, IN_MOVED_FROM, isArrayBuffer, isNumber, isObject, isString, link as sys_link, mkstemp, symlink as sys_symlink, tempnam, toArrayBuffer, toString, watch as iwatch, } from 'misc';
import { filename, mmap, munmap } from 'mmap';
import * as std from 'std';
import { SyscallError } from 'syscallerror';
import { TextDecoder } from 'textcode';
import { Directory } from 'directory';

let errno = 0;

const { O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_EXCL, O_TRUNC } = os ?? {
  O_RDONLY: 0x0,
  O_WRONLY: 0x1,
  O_RDWR: 0x2,
  O_APPEND: 0x400,
  O_CREAT: 0x40,
  O_EXCL: 0x80,
  O_TRUNC: 0x200,
};

const { EINVAL, EIO, EACCES, EEXIST, ENOSPC, ENOSYS, EBUSY, ENOENT, EPERM } = std.Error;

const EAGAIN = 11;
const EWOULDBLOCK = 11;

const COPY_BUF_SIZE = 65536;

export const stdin = std.in;
export const stdout = std.out;
export const stderr = std.err;

function MakeError(msg, errno, path, syscall) {
  errno = Math.abs(errno);
  return Object.assign(new Error(msg + (errno ? ': ' + std.strerror(errno) : '')), { errno: -errno, path, syscall });
}

export const constants = {
  COPYFILE_EXCL: 1,
  COPYFILE_FICLONE: 2,
  COPYFILE_FICLONE_FORCE: 4,
  F_OK: 0,
  X_OK: 1,
  W_OK: 2,
  R_OK: 4,
  O_APPEND: 1024,
  O_CREAT: 64,
  O_DIRECT: 16384,
  O_DIRECTORY: 65536,
  O_DSYNC: 4096,
  O_EXCL: 128,
  O_NOATIME: 262144,
  O_NOCTTY: 256,
  O_NOFOLLOW: 131072,
  O_NONBLOCK: 2048,
  O_RDONLY: 0,
  O_RDWR: 2,
  O_SYNC: 1052672,
  O_TRUNC: 512,
  O_WRONLY: 1,
  S_IFBLK: 24576,
  S_IFCHR: 8192,
  S_IFDIR: 16384,
  S_IFIFO: 4096,
  S_IFLNK: 40960,
  S_IFMT: 61440,
  S_IFREG: 32768,
  S_IFSOCK: 49152,
  S_IRGRP: 32,
  S_IROTH: 4,
  S_IRUSR: 256,
  S_IRWXG: 56,
  S_IRWXO: 7,
  S_IRWXU: 448,
  S_IWGRP: 16,
  S_IWOTH: 2,
  S_IWUSR: 128,
  S_IXGRP: 8,
  S_IXOTH: 1,
  S_IXUSR: 64,
};

export const InvalidBuffer = index => arg => new TypeError(`argument ${index} must be ArrayBuffer, TypedArray or DataView, but is: ${getTypeName(arg)}`);

(proto => {
  const { write, puts } = proto;

  if(!('writeb' in proto)) {
    Object.defineProperties(proto, {
      writeb: { value: write, configurable: true },
      write: {
        value(...args) {
          return (isString(args[0]) ? puts : write).call(this, ...args);
        },
        configurable: true,
      },
      isTTY: {
        get() {
          return os.isatty(this.fileno());
        },
        configurable: true,
      },
    });
  }
})(Object.getPrototypeOf(stdout));

function syscallerr(name, ret) {
  const { errno } = sys_error();

  if(typeof ret == 'number') {
    if(ret < 0) throw new SyscallError(name, errno == -ret ? -ret : errno);

    return ret || 0;
  } else if(Array.isArray(ret)) {
    const [str, err] = ret;

    if(err) throw new SyscallError(name, errno == err ? err : errno);

    return str;
  }

  throw new SyscallError(name, errno);
}

const statsFields = {
  mode: undefined,
  dev: undefined,
  ino: undefined,
  nlink: undefined,
  uid: undefined,
  gid: undefined,
  rdev: undefined,
  size: undefined,
  blocks: undefined,
  atime: undefined,
  mtime: undefined,
  ctime: undefined,
};

/**
 * File stat() properties
 *
 * @class      Stats (name)
 */
export class Stats {
  constructor(st) {
    this.mode = st.mode;

    for(let prop in statsFields)
      if(st[prop] !== undefined) {
        const value = st[prop];

        if(prop.endsWith('time')) prop += 'Ms';

        this[prop] = value;
      }

    for(const prop in statsFields) if(prop.endsWith('time')) this[prop] = new Date(this[prop + 'Ms']);
  }

  /* prettier-ignore */ isDirectory() { return (this.mode & os.S_IFMT) == os.S_IFDIR; }
  /* prettier-ignore */ isCharacterDevice() { return (this.mode & os.S_IFMT) == os.S_IFCHR; }
  /* prettier-ignore */ isBlockDevice() { return (this.mode & os.S_IFMT) == os.S_IFBLK; }
  /* prettier-ignore */ isFile() { return (this.mode & os.S_IFMT) == os.S_IFREG; }
  /* prettier-ignore */ isFIFO() { return (this.mode & os.S_IFMT) == os.S_IFIFO; }
  /* prettier-ignore */ isSymbolicLink() { return (this.mode & os.S_IFMT) == os.S_IFLNK; }
  /* prettier-ignore */ isSocket() { return (this.mode & os.S_IFMT) == os.S_IFSOCK; }
}

Stats.prototype[Symbol.toStringTag] = 'Stats';

for(const prop in statsFields)
  Object.defineProperty(Stats.prototype, prop, {
    value: undefined,
    enumerable: false,
    writable: true,
    configurable: true,
  });

delete Stats.prototype.constructor;

Object.setPrototypeOf(Stats.prototype, null);

/**
 * File stat() properties
 *
 * @class      Dirent (name)
 */
export class Dirent {
  name;
  #type;

  constructor(name, type) {
    this.name = name;
    this.#type = type;
  }

  /* prettier-ignore */ isDirectory() { return !!(this.#type & Directory.TYPE_DIR); }
  /* prettier-ignore */ isCharacterDevice() { return !!(this.#type & Directory.TYPE_CHR); }
  /* prettier-ignore */ isBlockDevice() { return !!(this.#type & Directory.TYPE_BLK); }
  /* prettier-ignore */ isFile() { return !!(this.#type & Directory.TYPE_REG); }
  /* prettier-ignore */ isFIFO() { return !!(this.#type & Directory.TYPE_FIFO); }
  /* prettier-ignore */ isSymbolicLink() { return !!(this.#type & Directory.TYPE_LNK); }
  /* prettier-ignore */ isSocket() { return !!(this.#type & Directory.TYPE_SOCK); }
}

Dirent.prototype[Symbol.toStringTag] = 'Dirent';

Object.defineProperty(Dirent.prototype, 'name', {
  value: undefined,
  enumerable: false,
  writable: true,
  configurable: true,
});

delete Dirent.prototype.constructor;

Object.setPrototypeOf(Dirent.prototype, null);

/**
 * Adapting quickjs-libc FILE object to fsPromises FileHandle API
 *
 * https://bellard.org/quickjs/quickjs.pdf (FILE prototype)
 *
 * https://nodejs.org/dist/latest-v10.x/docs/api/fs.html#fs_class_filehandle
 *
 * @class      FileHandle
 */
const fileObjs = new WeakMap();

const fileProto = (() => {
  const f = std.open('/dev/null', 'r');
  const p = Object.getPrototypeOf(f);
  f.close();
  return p;
})();

export class FileHandle {
  constructor(f) {
    fileObjs.set(this, f);
  }

  get fd() {
    return fileno(fileObjs.get(this));
  }

  appendFile(data, options = { encoding: 'utf8' }) {
    const encoding = isString(options) ? options : options.encoding;
    const file = fileObjs.get(this);

    if(isString(data)) file.puts(data);
    else file.write(data, 0, data.byteLength);

    file.flush();
  }

  chmod(mode) {
    if(sys_fchmod(this.fd, mode) < 0) {
      throw MakeError(`fchmod(${this.fd}, ${mode})`, sys_error().errno, undefined, 'fchmod');
    }
  }

  chown(uid, gid) {
    if(sys_fchown(this.fd, uid, gid) < 0) {
      throw MakeError(`fchown(${this.fd}, ${uid}, ${gid})`, sys_error().errno, undefined, 'fchown');
    }
  }

  close() {
    fileObjs.get(this).close();
  }

  datasync() {
    fileObjs.get(this).flush();

    if(sys_fdatasync(this.fd) < 0) {
      throw MakeError(`fdatasync(${this.fd})`, sys_error().errno, undefined, 'fdatasync');
    }
  }

  read(buffer, offset, length, position) {
    const args = throwIfNull(InvalidBuffer(1), bufferArguments, buffer, offset, length);

    const file = fileObjs.get(this);
    return { bytesRead: savePos(file, () => readSync(file, ...args), position), buffer: args[0] };
  }

  readFile(options = { encoding: null, flag: 'r' }) {
    const encoding = isString(options) ? options : options.encoding;
    const file = fileObjs.get(this);

    if(encoding == 'utf8') return file.readAsString();

    const filePos = file.tell();
    file.seek(0, std.SEEK_END);
    const size = file.tell();
    file.seek(filePos, std.SEEK_SET);

    const buffer = new ArrayBuffer(size);
    const bytesRead = file.read(buffer, 0, size);

    if(bytesRead == size) return buffer;
  }

  stat(options = { bigint: false }) {
    const [st, errno] = sys_fstat(this.fd, options);

    if(!st || errno) {
      throw MakeError(`fstat(${this.fd}, ...)`, errno, undefined, 'fstat');
    }

    return st;
  }

  sync() {
    fileObjs.get(this).flush();

    if(sys_fsync(this.fd) < 0) {
      throw MakeError(`fsync(${this.fd})`, sys_error().errno, undefined, 'fsync');
    }
  }

  truncate(len) {
    if(sys_ftruncate(this.fd, len) < 0) {
      throw MakeError(`ftruncate(${this.fd}, ${len})`, sys_error().errno, undefined, 'ftruncate');
    }
  }

  utimes(atime, mtime) {
    if(sys_futimes(this.fd, [atime, mtime]) < 0) {
      throw MakeError(`futimes(${this.fd}, [${atime}, ${mtime}])`, sys_error().errno, undefined, 'futimes');
    }
  }

  write(buffer, offset, length, position) {
    const file = fileObjs.get(this);
    const aBuf = validNumber(offset) && validNumber(length);

    if(!aBuf && isString(buffer)) {
      position = offset;
      return {
        bytesWritten: savePos(
          file,
          tellDiff(file, () => file.puts(buffer)),
          position,
        ),
        buffer,
      };
    }

    const args = throwIfNull(InvalidBuffer(1), stringOrBufferArguments, buffer, offset, length);

    return { bytesWritten: savePos(file, () => writeSync(file, ...args), position), buffer: args[0] };
  }

  writeFile(data, options = { encoding: 'utf8' }) {
    const encoding = isString(options) ? options : options.encoding;
    const file = fileObjs.get(this);

    if(isString(data)) file.puts(data);
    else file.write(data, 0, data.byteLength);

    file.flush();
  }
}

FileHandle.prototype[Symbol.toStringTag] = 'FileHandle';

delete FileHandle.prototype.constructor;

Object.setPrototypeOf(FileHandle.prototype, null);

function validNumber(n) {
  return Number.isFinite(Number(n));
}

function savePos(file, fn, position) {
  let savedPos;
  const doSave = validNumber(position);
  if(doSave) {
    savedPos = file.tell();
    file.seek(position, std.SEEK_SET);
  }
  const ret = fn();
  if(doSave) file.seek(savedPos, std.SEEK_SET);
  return ret;
}

function tellDiff(file, fn) {
  return () => {
    const beforePos = file.tell();
    fn();
    return file.tell() - beforePos;
  };
}

export const FD_CLOEXEC = 0o1;
export const F_DUPFD = 0o0;
export const F_DUPFD_CLOEXEC = 0o2006;
export const F_GETFD = 0o1;
export const F_GETFL = 0o3;
export const F_GETLK = 0o5;
export const F_RDLCK = 0o0;
export const F_SETFD = 0o2;
export const F_SETFL = 0o4;
export const F_SETLK = 0o6;
export const F_SETLKW = 0o7;
export const F_UNLCK = 0o2;
export const F_WRLCK = 0o1;
export const O_ASYNC = 0o20000;
export const O_CLOEXEC = 0o2000000;
export const O_DSYNC = 0o10000;
export const O_NOCTTY = 0o400;
export const O_NONBLOCK = 0o4000;
export const O_SYNC = 0o4010000;

/**
 * Create buffer
 *
 * @param      {Number}       length   The length
 * @return     {ArrayBuffer}  The array buffer.
 */
export function buffer(length) {
  return new ArrayBuffer(length);
}

/**
 * Create buffer from string
 *
 * @param      {String}  chunk   The chunk
 * @param      {Number}  offset  The offset
 * @param      {Number}  length  The length
 * @return     {Object}  The ArrayBuffer
 */
export function bufferFrom(chunk, offset, length) {
  return toArrayBuffer(chunk, offset, length);
}

/**
 * Get ArrayBuffer size
 *
 * @param      {Object}  buf     The buffer
 * @return     {Number}  ArrayBuffer size
 */
export function bufferSize(buf) {
  return buf.byteLength;
}

/**
 * Get ArrayBuffer object
 *
 * @param      {Object}  arg     ArrayBuffer, TypedArray or DataView
 * @return     {Object}
 */
export function bufferArgument(arg) {
  let args = null;

  if(isObject(arg)) {
    const props = ['buffer', 'byteOffset', 'byteLength'];

    if('buffer' in arg && isArrayBuffer(arg.buffer)) args = props.map((p, i) => arg[p] ?? [null, 0, Infinity][i]);
    else if(isArrayBuffer(arg)) args = [arg, 0, arg.byteLength];
  }

  return args;
}

/**
 * Get ArrayBuffer object
 *
 * @param      {Object}  arg     ArrayBuffer, TypedArray or DataView
 * @return     {Object}
 */
export function bufferArguments(arg, offset = 0, length = Infinity) {
  const args = bufferArgument(arg);

  if(args) {
    if(validNumber(offset)) {
      if(offset > args[2]) offset = args[2];

      args[1] += offset;
      args[2] -= offset;
    }

    if(validNumber(length)) if (args[2] > length) args[2] = length;

    if(!isArrayBuffer(args[0])) args = null;
  }

  //console.log('bufferArguments', { arg, offset, length, args });
  return args;
}

/**
 * Get ArrayBuffer object
 *
 * @param      {Object}  arg     ArrayBuffer, TypedArray or DataView
 * @return     {Object}
 */
export function stringOrBufferArguments(arg, ...args) {
  if(isString(arg)) arg = toArrayBuffer(arg);

  return bufferArguments(arg, ...args);
}

/**
 * Throw @param error if function @param fn returns null
 *
 * @param      {Object}   error
 * @param      {Function} fn
 * @param      {Array}    ...args
 * @return     {any}
 */
export function throwIfNull(error, fn, ...args) {
  const ret = fn(...args);

  if(ret === null) throw error;

  return ret;
}

/**
 * Convert ArrayBuffer to string
 *
 * @param      {Object}  buf     The buffer
 * @param      {Number}  offset  The offset
 * @param      {Number}  length  The length
 * @return     {String}
 */
export function bufferToString(buf, offset, length) {
  if(isString(buf)) return buf;
  return toString(buf, offset, length);
}

export function accessSync(pathname, mode) {
  if(sys_access(pathname, mode)) {
    errno = sys_error().errno;
    return -1;
  }

  return 0;
}

export function fopenSync(filename, flags = 'r', mode = 0o644) {
  const res = { errno: 0 };
  const file = std.open(filename, flags, res);

  if(!res.errno) return file;

  return syscallerr('fopen', -res.errno);
}

export function fdopenSync(fd, flags = 'r') {
  const res = { errno: 0 };
  const file = std.fdopen(fd, flags, res);

  if(!res.errno) return file;

  return syscallerr('fdopen', res.errno);
}

export function openSync(filename, flags = 'r', mode = 0o644) {
  if(isNumber(flags)) return syscallerr('fs.openSync', os.open(filename, flags, mode));

  return fopenSync(filename, flags, mode);
}

/* XXX: non-standard */
export function puts(fd, str) {
  if(isObject(fd) && typeof fd.puts == 'function') {
    fd.puts(str);
    fd.flush();
  } else {
    const data = toArrayBuffer(str);
    return os.write(fd, data, 0, data.byteLength);
  }
}

/* XXX: non-standard */
export function gets(fd) {
  if(isObject(fd) && typeof fd.getline == 'function') return fd.getline();

  const buf = new ArrayBuffer(4),
    u8 = new Uint8Array(buf);
  const dec = new TextDecoder('utf-8');
  let idx = 0,
    r,
    s = '',
    bytesRead = 0;

  while((r = os.read(fd, buf, idx, 1)) >= 1) {
    let tmp = dec.decode(buf.slice(idx, idx + 1));
    bytesRead += r;

    if(tmp.length > 0) {
      idx = 0;
      if(tmp == '\n') break;
      s += tmp;
    } else if(++idx == 4) {
      throw new Error(`fs.gets() decoding UTF-8: ${u8.map(n => n.toString(16)).join(' ')}`);
    }
  }

  return bytesRead ? s : null;
}

export function seek(fd, offset, whence) {
  let ret;

  switch (typeof fd) {
    case 'number':
      ret = syscallerr('lseek', os.seek(fd, offset, whence));
      break;
    default:
      if(syscallerr('fseek', fd.seek(offset, whence)) == 0) ret = typeof offset == 'bigint' ? fd.tello() : fd.tell();
      break;
  }

  return ret;
}

export function tell(file) {
  switch (typeof file) {
    case 'number':
      return syscallerr('lseek', os.seek(file, 0, std.SEEK_CUR));
    default:
      return file.tell();
  }
}

export function sizeSync(file) {
  const fd = isNumber(file) ? file : openSync(file, 'r');
  const pos = tell(fd);
  let bytes;
  if((bytes = seek(fd, 0, std.SEEK_END)) < 0) return bytes;
  if(file !== fd) closeSync(fd);
  return bytes;
}

export function nameSync(file) {
  const fd = isNumber(file) ? file : fileno(file);
  let ret;

  try {
    ret = readlinkSync(`/proc/self/fd/` + fd);
  } catch(e) {
    if(e.errno == SyscallError.ENOENT) throw new SyscallError('nameSync', SyscallError.EBADF);
  }

  if(isString(ret)) return ret;

  let size = Math.max(1024, sizeSync(fd));
  let map = mmap(0, size, undefined, undefined, fd, 0);
  let name = filename(map);
  munmap(map, size);

  return name;
}

export function getcwd() {
  return syscallerr('getcwd', os.getcwd());
}

export function chdir(path) {
  return syscallerr('chdir', os.chdir(path));
}

export function isatty(file) {
  const fd = fileno(file);
  return os.isatty(fd);
}

export function fileno(file) {
  return { number: f => f, object: f => (f && typeof f.fileno == 'function' ? f.fileno() : undefined) }[typeof file](file);
}

export function readFileSync(file, options = {}) {
  options = isString(options) ? { encoding: options } : options;
  options ??= {};

  if(options.encoding == 'utf-8') return std.loadFile(file);

  const res = { errno: 0 },
    f = std.open(file, 'r', res);

  if(!res.errno) {
    f.seek(0, std.SEEK_END);
    let size = f.tell();

    if(isNumber(size)) {
      f.seek(0, std.SEEK_SET);
      let data = new ArrayBuffer(size);
      f.read(data, 0, size);
      f.close();
      if(options.encoding != null) data = toString(data);
      return data;
    }
  }

  return syscallerr(`fs.readFileSync('${file}')`, -res.errno);
}

export function writeFileSync(file, data, options = { overwrite: true }) {
  const res = { errno: 0 };
  const f = openSync(file, (options?.flag ?? 'w') + (options?.overwrite ? '+' : ''), options?.mode ?? 0o666);

  if(res.errno) return syscallerr('fs.writeFileSync', -res.errno);

  if(isString(data)) {
    f.puts(data);
    f.flush();
    return f.tell();
  }

  const args = throwIfNull(InvalidBuffer(2), bufferArgument, data);

  //const fd = os.open(file, O_WRONLY | O_CREAT | (options?.overwrite ? O_TRUNC : O_EXCL), options?.mode ?? 0o666);

  const bytes = f.write(...args);
  if(options?.flush ?? true) f.flush();
  closeSync(f);
  return bytes;
}

export function appendFileSync(path, data, options = {}) {
  return writeFileSync(path, data, { ...options, flag: options.flag ?? 'a', overwrite: false });
}

export function closeSync(fd) {
  if(!isNumber(fd)) fd.flush();

  return syscallerr('close', isNumber(fd) ? os.close(fd) : fd.close());
}

export function linkSync(existingPath, newPath) {
  if(sys_link(existingPath, newPath) == -1) throw new SyscallError(`link`, sys_error().errno);
}

export function copyFileSync(src, dest, flags) {
  const { COPYFILE_EXCL, COPYFILE_FICLONE, COPYFILE_FICLONE_FORCE } = constants;

  if(flags & (COPYFILE_FICLONE | COPYFILE_FICLONE_FORCE)) {
    try {
      if(sys_link(src, dest) != -1) return;
    } catch(e) {}
    if(flags & COPYFILE_FICLONE_FORCE) throw new Error(`Unable to link '${src}' to '${dest}'`);
  }

  const { size, mode } = statSync(src);

  const r = openSync(src, 'rb'),
    w = openSync(dest, 'w' + (flags & COPYFILE_EXCL ? 'x' : '') + 'b', mode);

  const len = size < COPY_BUF_SIZE ? size : COPY_BUF_SIZE,
    rem = size < COPY_BUF_SIZE ? 0 : size % COPY_BUF_SIZE,
    ofs = 0;

  let pos = 0,
    buf = new ArrayBuffer(len);

  for(let i = 0; len + pos + rem <= size; i++, pos = len * i) {
    readSync(r, buf, ofs, len, pos);
    writeSync(w, buf, ofs, len, pos);
  }

  if(rem) {
    buf = new ArrayBuffer(rem);

    readSync(r, buf, ofs, rem, pos);
    writeSync(w, buf, ofs, rem, pos);
  }

  closeSync(r);
  closeSync(w);
}

export function cpSync(src, dest, options = {}) {
  const { recursive = false, force = true, errorOnExist = false } = options;

  const st = lstatSync(src);

  if(st.isDirectory()) {
    if(!recursive) throw new Error(`cpSync: '${src}' is a directory (pass { recursive: true })`);

    if(!existsSync(dest)) mkdirSync(dest, { recursive: true });
    else if(errorOnExist) throw new Error(`cpSync: '${dest}' already exists`);

    for(const name of readdirSync(src)) cpSync(join(src, name), join(dest, name), options);

    return;
  }

  if(existsSync(dest)) {
    if(errorOnExist) throw new Error(`cpSync: '${dest}' already exists`);
    if(!force) return;
  }

  copyFileSync(src, dest);
}

export function existsSync(path) {
  const [st, err] = os.stat(path);
  try {
    syscallerr('stat', [st, err]);
    return true;
  } catch(e) {}
}

export function lstatSync(path) {
  const [st, err] = os.lstat(path);
  return syscallerr('lstat', [st && new Stats(st), err]);
}

export function mkdirSync(path, options = 0o777) {
  const { recursive = false, mode = 0o777 } = isNumber(options) ? { mode: options } : (options ?? {});

  if(recursive) {
    const parent = dirname(path);

    if(parent && parent !== path && !existsSync(parent)) mkdirSync(parent, { recursive: true, mode });
    if(existsSync(path)) return path;
  }

  syscallerr('mkdir', os.mkdir(path, mode));

  return recursive ? path : undefined;
}

export function readSync(fd, buffer, offset, length) {
  const args = throwIfNull(InvalidBuffer(2), bufferArguments, buffer, offset, length);
  const ret = isNumber(fd) ? os.read(fd, ...args) : fd.read(...args);

  return syscallerr('read', ret);
}

export function readdirSync(path, { withFileTypes, recursive, encoding } = {}) {
  if(recursive) {
    const out = [];

    for(const entry of walkSync(path, { includeDirs: true, includeFiles: true, includeSymlinks: true })) {
      if(entry.path == path) continue; // Node's recursive readdirSync excludes the root itself

      const rel = entry.path.slice(path.length + (path.endsWith('/') ? 0 : 1));

      out.push(
        withFileTypes
          ? Object.assign(new Dirent(entry.name, entry.isDirectory ? Directory.TYPE_DIR : entry.isSymlink ? Directory.TYPE_LNK : Directory.TYPE_REG), {
              path: entry.path,
              parentPath: dirname(entry.path),
            })
          : rel,
      );
    }

    return out;
  }

  if(!withFileTypes) return syscallerr('readdir', os.readdir(path))?.filter(e => ['.', '..'].indexOf(e) == -1);

  const result = [];

  for(const [name, type] of new Directory(path)) {
    if(name == '.' || name == '..') continue;

    result.push(new Dirent(name, type));
  }

  return result;
}

export class Dir {
  #dir;
  #path;

  constructor(path) {
    this.#path = path;
    this.#dir = new Directory(path);
  }

  get path() {
    return this.#path;
  }

  readSync() {
    for(;;) {
      const { value, done } = this.#dir.next();

      if(done) return null;

      const [name, type] = value;

      if(name == '.' || name == '..') continue;

      return new Dirent(name, type);
    }
  }

  closeSync() {
    this.#dir.close();
  }

  [Symbol.iterator]() {
    return {
      next: () => {
        const entry = this.readSync();

        return entry ? { value: entry, done: false } : { value: undefined, done: true };
      },
    };
  }
}

export function opendirSync(path) {
  return new Dir(path);
}

/**
 * Recursive directory walk, modeled after Deno std/fs `walk()`/`walkSync()`
 * (the closest thing to a standardized cross-runtime recursive-readdir
 * interface - Node's `readdirSync(path, {recursive:true})` has no
 * filtering of its own) - a sync generator yielding `{path, name, isFile,
 * isDirectory, isSymlink}` entries, plus an extra `filter(entry, depth)`
 * predicate hook (not part of Deno's API) for filtering that `exts`/
 * `match`/`skip` can't express, e.g. depth-relative logic.
 *
 * `exts`/`match`/`skip` are only applied to non-directory entries, except
 * `skip` also prunes matching directories from being descended into at
 * all (so it's the cheap way to exclude e.g. `node_modules`-sized
 * subtrees without visiting them).
 *
 * @param      {String}    root              Directory to walk
 * @param      {Object}    [options]
 * @param      {Number}    [options.maxDepth=Infinity]        How many directory levels below `root` to descend
 * @param      {Boolean}   [options.includeFiles=true]        Yield file entries
 * @param      {Boolean}   [options.includeDirs=true]         Yield directory entries
 * @param      {Boolean}   [options.includeSymlinks=true]     Yield symlink entries (as themselves, when not following)
 * @param      {Boolean}   [options.followSymlinks=false]     Descend into/resolve symlinked directories instead of yielding them as symlinks
 * @param      {String[]}  [options.exts]                     Only yield files whose path ends with one of these suffixes
 * @param      {RegExp[]}  [options.match]                    Only yield files whose path matches at least one of these
 * @param      {RegExp[]}  [options.skip]                     Never yield, and never descend into, paths matching any of these
 * @param      {Function}  [options.filter]                   `(entry, depth) => boolean` extra predicate, applied last
 * @param      {Function}  [options.onError]                  `(error, dir) => void`; if omitted, a `readdirSync` failure (e.g. EACCES) is rethrown
 * @return     {Generator<{path,name,isFile,isDirectory,isSymlink}>}
 */
export function* walkSync(root, options = {}) {
  const { maxDepth = Infinity, includeFiles = true, includeDirs = true, includeSymlinks = true, followSymlinks = false, exts, match, skip, filter, onError } = options;

  function directoryEntryKind(path, dirent) {
    if(dirent.isSymbolicLink()) return 'symlink';
    if(dirent.isDirectory()) return 'dir';
    if(dirent.isFile()) return 'file';

    // d_type came back DT_UNKNOWN (some filesystems never set it) - fall back to lstat
    try {
      const st = lstatSync(path);
      if(st.isSymbolicLink()) return 'symlink';
      if(st.isDirectory()) return 'dir';
    } catch(e) {}

    return 'file';
  }

  function skipPath(path) {
    return !!(skip && skip.some(re => re.test(path)));
  }

  function fileIncluded(path) {
    if(exts && !exts.some(ext => path.endsWith(ext))) return false;
    if(match && !match.some(re => re.test(path))) return false;
    return true;
  }

  function* walk(dir, depth) {
    let entries;

    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch(e) {
      if(onError) onError(e, dir);
      else throw e;
      return;
    }

    for(const dirent of entries) {
      const path = join(dir, dirent.name);

      if(skipPath(path)) continue;

      const kind = directoryEntryKind(path, dirent);
      const isSymlink = kind == 'symlink';
      let isDirectory = kind == 'dir';

      if(isSymlink && followSymlinks) {
        try {
          isDirectory = statSync(path).isDirectory();
        } catch(e) {
          continue; // broken symlink
        }
      }

      const entry = { path, name: dirent.name, isFile: !isDirectory, isDirectory, isSymlink };

      if(isDirectory) {
        if(includeDirs && (!isSymlink || includeSymlinks) && (!filter || filter(entry, depth))) yield entry;

        if((!isSymlink || followSymlinks) && depth < maxDepth) yield* walk(path, depth + 1);
      } else {
        if(!includeFiles) continue;
        if(isSymlink && !includeSymlinks) continue;
        if(!fileIncluded(path)) continue;
        if(filter && !filter(entry, depth)) continue;

        yield entry;
      }
    }
  }

  yield* walk(root, 1);
}

export function readlinkSync(path) {
  return syscallerr('readlink', os.readlink(path));
}

export function realpathSync(path) {
  return syscallerr('realpath', os.realpath(path));
}

export function renameSync(oldname, newname) {
  return syscallerr('rename', os.rename(oldname, newname));
}

export function statSync(path) {
  let [st, err] = os.stat(path);
  return syscallerr('stat', [st && new Stats(st), err]);
}

export function symlinkSync(target, path) {
  /* try {
    sys_symlink(target, path);
  } catch(e) {
    return -e.errno;
  }*/
  return syscallerr('symlink', sys_symlink(target, path));
}

export function tmpfileSync() {
  const err = { errno: 0 };
  const f = std.tmpfile(err);

  if(err.errno) return syscallerr('tmpfile', -err.errno);

  return f;
}

export function mkstempSync(template) {
  return syscallerr('mkstemp', mkstemp(template));
}

export function mkdtempSync(prefix) {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';

  for(let attempt = 0; attempt < 100; attempt++) {
    let suffix = '';

    for(let i = 0; i < 6; i++) suffix += chars[Math.floor(Math.random() * chars.length)];

    const dir = prefix + suffix;

    try {
      mkdirSync(dir);
      return dir;
    } catch(e) {
      if(e?.errno !== EEXIST) throw e;
    }
  }

  throw new Error(`mkdtempSync: could not create a unique directory under '${prefix}'`);
}

/* No path-based chmod(2)/chown(2)/truncate(2) binding is exposed by 'os' or
 * 'misc' (only the fd-based fchmod/fchown/ftruncate FileHandle uses) - open
 * the path read-only and use those instead, matching chmod(2)'s own
 * fd-vs-path equivalence (unlike lchmod/lchown, this doesn't need the
 * no-follow behavior a symlink would require). */
export function chmodSync(path, mode) {
  const fd = syscallerr('open', os.open(path, O_RDONLY));

  try {
    if(sys_fchmod(fd, mode) < 0) throw new SyscallError('chmod', sys_error().errno);
  } finally {
    os.close(fd);
  }
}

export function chownSync(path, uid, gid) {
  const fd = syscallerr('open', os.open(path, O_RDONLY));

  try {
    if(sys_fchown(fd, uid, gid) < 0) throw new SyscallError('chown', sys_error().errno);
  } finally {
    os.close(fd);
  }
}

export function truncateSync(path, len = 0) {
  const fd = syscallerr('open', os.open(path, O_WRONLY));

  try {
    if(sys_ftruncate(fd, len) < 0) throw new SyscallError('truncate', sys_error().errno);
  } finally {
    os.close(fd);
  }
}

export function utimesSync(path, atime, mtime) {
  const toMs = t => (t instanceof Date ? t.getTime() : Number(t) * 1000);

  return syscallerr('utimes', os.utimes(path, toMs(atime), toMs(mtime)));
}

export function tempnamSync(dir, pfx) {
  let base = process.argv[1] ?? process.argv[0];
  pfx ??= basename(base, extname(base)) + '-';

  return tempnam(dir, pfx);
}

export function unlinkSync(path) {
  return syscallerr('unlink', os.remove(path));
}

export function rmdirSync(path) {
  return syscallerr('rmdir', os.remove(path));
}

export function rmSync(path, options = {}) {
  const { recursive = false, force = false } = options;

  let st;

  try {
    st = lstatSync(path);
  } catch(e) {
    if(force && e?.errno === ENOENT) return;
    throw e;
  }

  if(st.isDirectory()) {
    if(!recursive) return rmdirSync(path);

    for(const name of readdirSync(path)) rmSync(join(path, name), options);

    return rmdirSync(path);
  }

  return unlinkSync(path);
}

export function writeSync(fd, buffer, offset, length) {
  const args = throwIfNull(InvalidBuffer(2), stringOrBufferArguments, buffer, offset, length);
  let ret;

  isNumber(fd) ? (ret = os.write(fd, ...args)) : ((ret = fd.write(...args)), fd.flush());

  return syscallerr('write', ret);
}

export function flushSync(f) {
  if(typeof f == 'object' && f !== null && 'flush' in f) {
    f.flush();
    return true;
  }
}

export function pipe() {
  let [rd, wr] = os.pipe();
  return [rd, wr];
}

export function onRead(file, handler = null) {
  os.setReadHandler(fileno(file), handler);
}

export function waitRead(file) {
  const fd = fileno(file);

  return new Promise((resolve, reject) =>
    os.setReadHandler(fd, () => {
      os.setReadHandler(fd, null);
      resolve(file);
    }),
  );
}

export function onWrite(file, handler = null) {
  os.setWriteHandler(fileno(file), handler);
}

export function waitWrite(file) {
  const fd = fileno(file);

  return new Promise((resolve, reject) =>
    os.setWriteHandler(fd, () => {
      os.setWriteHandler(fd, null);
      resolve(file);
    }),
  );
}

export function* readerSync(input, bufferOrBufSize = 1024) {
  const buf = isNumber(bufferOrBufSize) ? buffer(bufferOrBufSize) : bufferOrBufSize;
  let ret;

  while((ret = readSync(input, buf, 0, buf.byteLength)) > 0) yield buf.slice(0, ret);

  return closeSync(input);
}

export function readAllSync(input, bufSize = 1024) {
  let s = '';
  const dec = new TextDecoder('utf-8');

  for(const chunk of readerSync(input, bufSize)) s += dec.decode(chunk);
  return s;
}

export function readFullySync(input, buf, start = 0, n) {
  n ??= buf.byteLength;

  while(start < n) {
    const r = readSync(input, buf, start, n - start);
    if(r <= 0) return new Error(`input ${r < 0 ? 'error' : 'closed'} ${start} of ${n} bytes read`);
    start += r;
  }

  return start;
}

export class inotify_event extends ArrayBuffer {
  constructor(obj = {}) {
    super(24);
    Object.assign(this, obj);
  }

  /* 0: int wd */
  set wd(value) {
    new Int32Array(this, 0, 1)[0] = value;
  }
  get wd() {
    return new Int32Array(this, 0, 1)[0];
  }

  /* 4: uint32_t (unsigned int) mask */
  set mask(value) {
    new Uint32Array(this, 4, 1)[0] = value;
  }
  get mask() {
    return new Uint32Array(this, 4, 1)[0];
  }

  /* 8: uint32_t (unsigned int) cookie */
  set cookie(value) {
    new Uint32Array(this, 8, 1)[0] = value;
  }
  get cookie() {
    return new Uint32Array(this, 8, 1)[0];
  }

  /* 12: uint32_t (unsigned int) len */
  set len(value) {
    new Uint32Array(this, 12, 1)[0] = value;
  }
  get len() {
    return new Uint32Array(this, 12, 1)[0];
  }

  /* 16: char [] name */
  set name(value) {
    if(isObject(value) && value instanceof ArrayBuffer) value = toPointer(value);
    new Int8Array(this, 16)[0] = value;
  }
  get name() {
    return new Int8Array(this, 16)[0];
  }

  static from(arg) {
    return Object.setPrototypeOf(toArrayBuffer(arg, 20), inotify_event.prototype);
  }

  toString() {
    const { wd, mask, cookie, len, name } = this;
    return `inotify_event {\n\t.wd = ${wd},\n\t.mask = ${mask},\n\t.cookie = ${cookie},\n\t.len = ${len},\n\t.name = ${name}\n}`;
  }

  [Symbol.toStringTag] = 'inotify_event';
}

/**
 * Node-compatible fs.watch(): watches a single path for changes, emitting
 * 'change'/'rename' (matching Node's eventType values) with the affected
 * entry's name, via the FSWatcher (EventEmitter) return value and/or the
 * optional callback(eventType, filename) argument.
 */
export function watch(filename, options = {}, callback = (eventType, filename) => {}) {
  if(isString(options)) options = { encoding: options };
  if(isNumber(options)) options = { mask: options };

  const { mask = IN_ALL_EVENTS, signal } = options;

  /* Node throws synchronously for an invalid path; let the native
   * ENOENT/etc. SyscallError propagate the same way. */
  const fd = iwatch();
  const wd = iwatch(fd, filename, mask);

  const ret = new (class FSWatcher extends EventEmitter {
    close() {
      if(this.closed) return;
      this.closed = true;
      os.setReadHandler(fd, null);
      iwatch(fd, wd);
      os.close(fd);
      this.emit('close');
    }
  })();

  ret.on('change', (eventType, filename) => callback('change', filename));
  ret.on('rename', (eventType, filename) => callback('rename', filename));

  const buf = new ArrayBuffer(4096);
  let bytes = 0;

  os.setReadHandler(fd, () => {
    const r = os.read(fd, buf, bytes, buf.byteLength - bytes);

    if(r > 0) {
      bytes += r;

      for(const ev of iwatch(buf, 0, bytes)) {
        if(ev.mask & IN_CLOSE_NOWRITE) continue;

        const type = ev.mask & (IN_CREATE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_DELETE_SELF) ? 'rename' : 'change';
        ret.emit(type, ev.name);
      }

      bytes = 0;
    } else if(r < 0) {
      ret.emit('error', MakeError(`watch('${filename}')`, sys_error().errno, filename, 'watch'));
      ret.close();
    }
  });

  if(signal) {
    if(signal.aborted) ret.close();
    else signal.addEventListener('abort', () => ret.close(), { once: true });
  }

  return ret;
}

export function createReadStream(path, options = { encoding: 'utf8' }) {
  const { encoding, bufSize = 1024, start = 0, end = Infinity, flags = 'r' } = isString(options) ? { encoding: options } : options;

  let err = {},
    buf,
    decoder,
    bytesRead = 0;

  return std.open(path, flags + 'b', err);
}

export function createWriteStream(path, options = { encoding: 'utf8' }) {
  let { encoding, start = 0, flags } = isString(options) ? { encoding: options } : options;

  let err = {},
    bytesWritten = 0,
    encoder;

  flags ??= start > 0 ? 'r+' : 'w';

  return std.open(path, flags + 'b', err);
}

const CharWidth = {
  1: Uint8Array,
  2: Uint16Array,
  4: Uint32Array,
};

function Encoding2Bytes(encoding) {
  switch (encoding.toLowerCase()) {
    case 'utf8':
    case 'utf-8':
      return 1;
    case 'utf16':
    case 'utf-16':
      return 2;
    case 'utf32':
    case 'utf-32':
      return 4;
  }
}

export default {
  O_RDONLY,
  O_WRONLY,
  O_RDWR,
  O_APPEND,
  O_CREAT,
  O_EXCL,
  O_TRUNC,
  O_RDONLY,
  O_WRONLY,
  O_RDWR,
  O_APPEND,
  O_CREAT,
  O_EXCL,
  O_TRUNC,
  EINVAL,
  EIO,
  EACCES,
  EEXIST,
  ENOSPC,
  ENOSYS,
  EBUSY,
  ENOENT,
  EPERM,
  EAGAIN,
  EWOULDBLOCK,
  FD_CLOEXEC,
  F_DUPFD,
  F_DUPFD_CLOEXEC,
  F_GETFD,
  F_GETFL,
  F_GETLK,
  F_RDLCK,
  F_SETFD,
  F_SETFL,
  F_SETLK,
  F_SETLKW,
  F_UNLCK,
  F_WRLCK,
  O_ASYNC,
  O_CLOEXEC,
  O_DSYNC,
  O_NOCTTY,
  O_NONBLOCK,
  O_SYNC,
  Stats,
  accessSync,
  buffer,
  bufferFrom,
  bufferSize,
  bufferToString,
  chdir,
  closeSync,
  createReadStream,
  createWriteStream,
  existsSync,
  fdopenSync,
  fileno,
  fopenSync,
  getcwd,
  gets,
  isatty,
  lstatSync,
  mkdirSync,
  mkstempSync,
  nameSync,
  onRead,
  onWrite,
  openSync,
  pipe,
  puts,
  readAllSync,
  readFileSync,
  readSync,
  readdirSync,
  readerSync,
  readlinkSync,
  realpathSync,
  renameSync,
  seek,
  sizeSync,
  statSync,
  stderr,
  stdin,
  stdout,
  symlinkSync,
  tell,
  tempnamSync,
  tmpfileSync,
  unlinkSync,
  walkSync,
  waitRead,
  waitWrite,
  watch,
  writeFileSync,
  writeSync,
};
