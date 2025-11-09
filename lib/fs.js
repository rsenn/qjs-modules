import { EventEmitter } from 'events';
import { basename, extname } from 'path';
import { filename, mmap, munmap } from 'mmap';
import * as std from 'std';
import * as os from 'os';
import { IN_ATTRIB, IN_CLOSE_WRITE, IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_MODIFY, IN_MOVED_TO, IN_MOVE_SELF, access as sys_access, error as sys_error, fchmod as sys_fchmod, fchown as sys_fchown, fdatasync as sys_fdatasync, fstat as sys_fstat, fsync as sys_fsync, ftruncate as sys_ftruncate, futimes as sys_futimes, link as sys_link, symlink as sys_symlink, isNumber, isString, isObject, isArrayBuffer, mkstemp, tempnam, toArrayBuffer, toString, watch, } from 'misc';
import { TextEncoder, TextDecoder } from 'textcode';
import { SyscallError } from 'syscallerror';

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
const EAGAIN = 11,
  EWOULDBLOCK = 11;

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

const InvalidBuffer = index => arg => new TypeError(`argument ${index} must be ArrayBuffer, TypedArray or DataView, but is: ${getTypeName(arg)}`);

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

    for(const prop in statsFields) {
      if(prop.endsWith('time')) this[prop] = new Date(this[prop + 'Ms']);
    }
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
  if(isNumber(flags)) return syscallerr('open', os.open(filename, flags, mode));

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

  return syscallerr('open', -res.errno);
}

export function writeFileSync(file, data, options = { overwrite: true }) {
  const res = { errno: 0 };
  const f = openSync(file, (options?.flag ?? 'w') + (options?.overwrite ? '+' : ''), options?.mode ?? 0o666);

  if(res.errno) return syscallerr('open', -res.errno);

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

export function closeSync(fd) {
  if(!isNumber(fd)) fd.flush();

  return syscallerr('close', isNumber(fd) ? os.close(fd) : fd.close());
}

export async function link(existingPath, newPath) {
  if(sys_link(existingPath, newPath) == -1) throw new SyscallError(`link`, sys_error().errno);
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

export function mkdirSync(path, mode = 0o777) {
  return syscallerr('mkdir', os.mkdir(path, mode));
}

export function readSync(fd, buffer, offset, length) {
  const args = throwIfNull(InvalidBuffer(2), bufferArguments, buffer, offset, length);
  const ret = isNumber(fd) ? os.read(fd, ...args) : fd.read(...args);

  return syscallerr('read', ret);
}

export function readdirSync(path) {
  return syscallerr('readdir', os.readdir(path));
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
  const err = {};
  std.tmpfile(err);
  return syscallerr('tmpfile', err.errno);
}

export function mkstempSync(template) {
  return syscallerr('mkstemp', mkstemp(template));
}

export function tempnamSync(dir, pfx) {
  let base = process.argv[1] ?? process.argv[0];
  pfx ??= basename(base, extname(base)) + '-';

  return tempnam(dir, pfx);
}

export function unlinkSync(path) {
  return syscallerr('unlink', os.remove(path));
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

  return new Promise((resolve, reject) => {
    os.setReadHandler(fd, () => {
      os.setReadHandler(fd, null);
      resolve(file);
    });
  });
}

export function onWrite(file, handler = null) {
  os.setWriteHandler(fileno(file), handler);
}

export function waitWrite(file) {
  const fd = fileno(file);

  return new Promise((resolve, reject) => {
    os.setWriteHandler(fd, () => {
      os.setWriteHandler(fd, null);
      resolve(file);
    });
  });
}

export function reader(input, bufferOrBufSize = 1024) {
  const buf = isNumber(bufferOrBufSize) ? buffer(bufferOrBufSize) : bufferOrBufSize;
  let ret;

  return {
    [Symbol.asyncIterator]: () => ({
      async next(numBytes = buf.byteLength) {
        let ret;
        let received = 0;

        do {
          if((ret = await read(input, buf, received, Math.min(numBytes - received, buf.byteLength))) <= 0) break;

          received += ret;
        } while(received < numBytes && numBytes < buf.byteLength);

        if(ret <= 0) {
          closeSync(input);
          return { done: true };
        }

        return { done: false, value: buf.slice(0, received) };
      },
    }),
  };
}

export function* readerSync(input, bufferOrBufSize = 1024) {
  const buf = isNumber(bufferOrBufSize) ? buffer(bufferOrBufSize) : bufferOrBufSize;
  let ret;

  while((ret = readSync(input, buf, 0, buf.byteLength)) > 0) yield buf.slice(0, ret);

  return closeSync(input);
}

export function readAllSync(input, bufSize = 1024) {
  let s = '';
  for(const chunk of readerSync(input, bufSize)) s += toString(chunk);
  return s;
}

export function readFullySync(input, buf, start = 0, n) {
  n ??= buf.byteLength;

  while(start < n) {
    let r = readSync(input, buf, start, n - start);

    if(r <= 0) return new Error(`input ${r < 0 ? 'error' : 'closed'} ${start} of ${n} bytes read`);

    start += r;
  }

  return start;
}

export async function readFully(input, buf, start = 0, n) {
  n ??= buf.byteLength;

  while(start < n) {
    let r = await read(input, buf, start, n - start);

    if(r <= 0) return new Error(`input ${r < 0 ? 'error' : 'closed'} ${start} of ${n} bytes read`);

    start += r;
  }

  return start;
}

export async function open(filename, flags = 'file', mode = 0o644) {
  const errorObj = { errno: 0 };
  const file = std.open(filename, flags, errorObj);

  if(file == null || errorObj.errno) {
    throw MakeError(`Error opening '${filename}' (${flags})`, -errorObj.errno, filename, 'open');
  }

  return new FileHandle(file);
}

export async function close(fd) {
  return closeSync(fd);
}

export async function copyFile(src, dest) {
  const { COPYFILE_EXCL, COPYFILE_FICLONE, COPYFILE_FICLONE_FORCE } = constants;

  if(flags & (COPYFILE_FICLONE | COPYFILE_FICLONE_FORCE)) {
    if(sys_link(src, dest) != -1) return;
    if(flags & COPYFILE_FICLONE_FORCE) throw new Error(`Unable to link '${src}' to '${dest}'`);
  }

  const { size, mode } = await sys_stat(src);

  const r = await open(src, 'rb'),
    w = await open(dest, 'wb', mode);

  const len = size < COPY_BUF_SIZE ? size : COPY_BUF_SIZE,
    rem = size < COPY_BUF_SIZE ? 0 : size % COPY_BUF_SIZE,
    ofs = 0;

  let pos = 0,
    buf = new ArrayBuffer(len);

  for(let i = 0; len + pos + rem <= size; i++, pos = len * i) {
    await read(r, buf, ofs, len, pos);
    await write(w, buf, ofs, len, pos);
  }

  if(rem) {
    buf = new ArrayBuffer(rem);

    await read(r, buf, ofs, rem, pos);
    await write(w, buf, ofs, rem, pos);
  }

  await close(r);
  await close(w);
}

export async function exists(path) {
  return existsSync(path);
}

export async function lstat(path) {
  return lstatSync(path);
}

export async function mkdir(path) {
  return mkdirSync(path);
}

export async function read(fd, buf, offset, length) {
  const args = throwIfNull(InvalidBuffer(2), bufferArguments, buf, offset, length);
  let ret;

  do {
    await waitRead(fd);
    errno = 0;
    ret = readSync(fd, ...args);
  } while(ret == -1 && errno == EAGAIN);

  return ret;
}

export async function stat(path) {
  return statSync(path);
}

export async function symlink(target, path) {
  return symlinkSync(target, path);
}

export async function tmpfile() {
  return tmpfileSync();
}

export async function write(fd, buf, offset, length) {
  const args = throwIfNull(InvalidBuffer(2), stringOrBufferArguments, buf, offset, length);
  let ret;

  do {
    await waitWrite(fd);
    errno = 0;
    ret = writeSync(fd, ...args);
  } while(ret == -1 && errno == EWOULDBLOCK);

  return ret;
}

export async function readAll(input, bufSize = 1024) {
  let s = '';
  for await(const chunk of reader(input, bufSize)) s += toString(chunk);
  return s;
}

export class inotify_event extends ArrayBuffer {
  constructor(obj = {}) {
    super(24);
    Object.assign(this, obj);
  }

  /* 0: int wd */
  set wd(value) {
    new Int32Array(this, 0)[0] = value;
  }
  get wd() {
    return new Int32Array(this, 0)[0];
  }

  /* 4: uint32_t (unsigned int) mask */
  set mask(value) {
    new Uint32Array(this, 4)[0] = value;
  }
  get mask() {
    return new Uint32Array(this, 4)[0];
  }

  /* 8: uint32_t (unsigned int) cookie */
  set cookie(value) {
    new Uint32Array(this, 8)[0] = value;
  }
  get cookie() {
    return new Uint32Array(this, 8)[0];
  }

  /* 12: uint32_t (unsigned int) len */
  set len(value) {
    new Uint32Array(this, 12)[0] = value;
  }
  get len() {
    return new Uint32Array(this, 12)[0];
  }

  /* 16: char [] name */
  set name(value) {
    if(isObject(value) && value instanceof ArrayBuffer) value = toPointer(value);
    new Int8Array(this, 16)[0] = value;
  }
  get name() {
    return new Int8Array(this, 16)[0];
  }

  static from(address) {
    let ret = toArrayBuffer(address, 20);
    return Object.setPrototypeOf(ret, inotify_event.prototype);
  }

  toString() {
    const { wd, mask, cookie, len, name } = this;
    return `inotify_event {\n\t.wd = ${wd},\n\t.mask = ${mask},\n\t.cookie = ${cookie},\n\t.len = ${len},\n\t.name = ${name}\n}`;
  }

  [Symbol.toStringTag] = 'inotify_event';
}

export function watch(filename, options = {}, callback = (eventType, filename) => {}) {
  let fd, wd, buf, arr, ret;

  if(isString(options)) options = { encoding: options };
  if(isNumber(options)) options = { mask: options };

  ret = new EventEmitter();

  ret.on('change', filename => callback('change', filename));
  ret.on('rename', filename => callback('rename', filename));

  options.mask ??= IN_MODIFY | IN_MOVE_SELF | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF | IN_CREATE | IN_CLOSE_WRITE | IN_ATTRIB;

  try {
    fd = watch();
    wd = watch(fd, filename, options.mask);
  } catch(err) {
    return err;
  }

  buf = new ArrayBuffer(16);
  arr = new Uint32Array(buf);

  os.setReadHandler(fd, () => {
    //std.puts(`readHandler fd=${fd}\n`);
    let r = os.read(fd, buf, 0, buf.byteLength);

    const [wd, mask, cookie, len] = arr;
    let name = '';

    if(len > 0) {
      buf = new ArrayBuffer(len);
      r = os.read(fd, buf, 0, len);

      name = toString(buf.slice(0, r));
    }

    let event = {
      wd,
      mask,
      cookie,
      name,
    };

    //std.puts(`readHandler r=${r}\n`);

    ret.emit('change', filename);
  });

  //std.puts(`watch ret=${ret}\n`);

  return ret;
}

export function createReadStream(path, options = { encoding: 'utf8' }) {
  const { encoding, bufSize = 1024, start = 0, end = Infinity, flags = 'r' } = isString(options) ? { encoding: options } : options;

  let file,
    err = {},
    buf,
    decoder,
    bytesRead = 0;

  if(encoding) decoder = new TextDecoder(encoding);

  return new ReadableStream({
    start(controller) {
      console.log('ReadableStream.start', controller);
      file = std.open(path, flags + 'b', err);

      if(start > 0) file.seek(start, std.SEEK_SET);

      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
    },
    pull(controller) {
      console.log('ReadableStream.pull', controller);
      if(file.eof()) {
        controller.close();
        return;
      }

      buf ??= new ArrayBuffer(bufSize);

      const readLen = Number.isFinite(end) ? Math.min(bufSize, end - bytesRead) : bufSize;
      const ret = readLen > 0 ? file.read(buf, 0, readLen) : 0;

      if(ret > 0) {
        bytesRead += ret;
        let data = buf.slice(0, ret);
        if(decoder) data = decoder.decode(data);

        controller.enqueue(data);
      } else if(file.sys_error()) {
        controller.sys_error(file);
      } else {
        controller.close();
      }
    },
    cancel(reason) {
      console.log('ReadableStream.cancel', controller);
      file.close();
    },
  });
}

export function createWriteStream(path, options = { encoding: 'utf8' }) {
  let { encoding, start = 0, flags } = isString(options) ? { encoding: options } : options;

  let file,
    err = {},
    bytesWritten = 0,
    encoder;

  flags ??= start > 0 ? 'r+' : 'w';

  return new WritableStream({
    start(controller) {
      console.log('WritableStream.start', controller);
      file = std.open(path, flags + 'b', err);
      if(start > 0) file.seek(start, std.SEEK_SET);
      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
    },
    write(chunk, controller) {
      console.log('WritableStream.write', chunk, controller);
      if(isString(chunk)) {
        encoder ??= new TextEncoder(encoding);
        chunk = encoder.encode(chunk).buffer;
      }
      const ret = file.write(chunk, 0, chunk.byteLength);
      file.flush();
      if(ret > 0) bytesWritten += ret;
      if(file.error()) controller.error(file);
    },
    close(controller) {
      console.log('WritableStream.close', controller);
      file.close();
    },
    abort(reason) {
      console.log('WritableStream.abort', reason);
      file.close();
    },
  });
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
  close,
  closeSync,
  createReadStream,
  createWriteStream,
  exists,
  existsSync,
  fdopenSync,
  fileno,
  fopenSync,
  getcwd,
  gets,
  isatty,
  lstat,
  lstatSync,
  mkdir,
  mkdirSync,
  mkstempSync,
  nameSync,
  onRead,
  onWrite,
  open,
  openSync,
  pipe,
  puts,
  read,
  readAll,
  readAllSync,
  readFileSync,
  readSync,
  readdirSync,
  reader,
  readerSync,
  readlinkSync,
  realpathSync,
  renameSync,
  seek,
  sizeSync,
  stat,
  statSync,
  stderr,
  stdin,
  stdout,
  symlink,
  symlinkSync,
  tell,
  tempnamSync,
  tmpfile,
  tmpfileSync,
  unlinkSync,
  waitRead,
  waitWrite,
  watch,
  write,
  writeFileSync,
  writeSync,
};
