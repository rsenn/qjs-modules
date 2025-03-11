import { EventEmitter } from 'events';
import { basename, extname } from 'path';
import { filename, mmap, munmap } from 'mmap';
import * as std from 'std';
import * as os from 'os';
import * as misc from 'misc';
import { F_OK, R_OK, W_OK, X_OK, fchmod, fchown, fsync, fdatasync, error, futimes, fstat, ftruncate, toArrayBuffer } from 'misc';
export { F_OK, R_OK, W_OK, X_OK } from 'misc';
import { TextEncoder, TextDecoder } from 'textcode';

let errno = 0;

const { O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_EXCL, O_TRUNC } = os ?? {
  O_RDONLY: 0x0,
  O_WRONLY: 0x1,
  O_RDWR: 0x2,
  O_APPEND: 0x400,
  O_CREAT: 0x40,
  O_EXCL: 0x80,
  O_TRUNC: 0x200
};

const { EINVAL, EIO, EACCES, EEXIST, ENOSPC, ENOSYS, EBUSY, ENOENT, EPERM } = std.Error;
const EAGAIN = 11,
  EWOULDBLOCK = 11;

export const stdin = std.in;
export const stdout = std.out;
export const stderr = std.err;

function MakeError(msg, errno, path, syscall) {
  errno = Math.abs(errno);
  return Object.assign(new Error(msg + (errno ? ': ' + std.strerror(errno) : '')), { errno: -errno, path, syscall });
}

(proto => {
  const { write, puts } = proto;

  if(!('writeb' in proto)) {
    Object.defineProperties(proto, {
      writeb: { value: write, configurable: true },
      write: {
        value(...args) {
          return (typeof args[0] == 'string' ? puts : write).call(this, ...args);
        },
        configurable: true
      },
      isTTY: {
        get() {
          return os.isatty(this.fileno());
        },
        configurable: true
      }
    });
  }
})(Object.getPrototypeOf(stdout));

function strerr(ret) {
  const [str, err] = ret;

  if(err) {
    errno = err;
    return null;
  }

  return str;
}

function numerr(ret) {
  if(ret < 0) {
    errno = -ret;
    return -1;
  }

  return ret || 0;
}

function objerr(fn, ...args) {
  let ret,
    err = {};

  if((ret = fn(...args, err)) === null) {
    errno = err.errno;
    return -1;
  }

  return ret;
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
  ctime: undefined
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

    for(let prop in statsFields) {
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

for(let prop in statsFields) Object.defineProperty(Stats.prototype, prop, { value: undefined, enumerable: false, writable: true, configurable: true });

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

export class FileHandle {
  constructor(f) {
    fileObjs.set(this, f);
  }

  get fd() {
    return fileno(fileObjs.get(this));
  }

  appendFile(data, options = { encoding: 'utf8' }) {
    const encoding = typeof options == 'string' ? options : options.encoding;
    const file = fileObjs.get(this);

    if(typeof data == 'string') file.puts(data);
    else file.write(data, 0, data.byteLength);
  }

  chmod(mode) {
    if(fchmod(this.fd, mode) < 0) {
      throw MakeError(`fchmod(${this.fd}, ${mode})`, error().errno, undefined, 'fchmod');
    }
  }

  chown(uid, gid) {
    if(fchown(this.fd, uid, gid) < 0) {
      throw MakeError(`fchown(${this.fd}, ${uid}, ${gid})`, error().errno, undefined, 'fchown');
    }
  }

  close() {
    fileObjs.get(this).close();
  }

  datasync() {
    fileObjs.get(this).flush();

    if(fdatasync(this.fd) < 0) {
      throw MakeError(`fdatasync(${this.fd})`, error().errno, undefined, 'fdatasync');
    }
  }

  read(buffer, offset, length, position) {
    const file = fileObjs.get(this);
    return { bytesRead: savePos(file, () => file.read(buffer, offset, length), position), buffer };
  }

  readFile(options = { encoding: null, flag: 'r' }) {
    const encoding = typeof options == 'string' ? options : options.encoding;
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
    const [st, errno] = fstat(this.fd, options);

    if(!st || errno) {
      throw MakeError(`fstat(${this.fd}, ...)`, errno, undefined, 'fstat');
    }

    return st;
  }

  sync() {
    fileObjs.get(this).flush();

    if(fsync(this.fd) < 0) {
      throw MakeError(`fsync(${this.fd})`, error().errno, undefined, 'fsync');
    }
  }

  truncate(len) {
    if(ftruncate(this.fd, len) < 0) {
      throw MakeError(`ftruncate(${this.fd}, ${len})`, error().errno, undefined, 'ftruncate');
    }
  }

  utimes(atime, mtime) {
    if(futimes(this.fd, [atime, mtime]) < 0) {
      throw MakeError(`futimes(${this.fd}, [${atime}, ${mtime}])`, error().errno, undefined, 'futimes');
    }
  }

  write(buffer, offset, length, position) {
    const file = fileObjs.get(this);
    const aBuf = validNumber(offset) && validNumber(length);

    if(!aBuf) {
      /* filehandle.write(string[, position[, encoding]]) */
      position = offset;
      //const encoding = length;
      return {
        bytesWritten: savePos(
          file,
          tellDiff(file, () => file.puts(buffer)),
          position
        ),
        buffer
      };
    }

    /* filehandle.write(buffer, offset, length, position) */
    if(typeof buffer == 'string') buffer = toArrayBuffer(buffer);
    return { bytesWritten: savePos(file, () => file.write(buffer, offset, length), position), buffer };
  }
  writeFile(data, options = { encoding: 'utf8' }) {
    const encoding = typeof options == 'string' ? options : options.encoding;
    const file = fileObjs.get(this);

    if(typeof data == 'string') file.puts(data);
    else file.write(data, 0, data.byteLength);
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
  return misc.toArrayBuffer(chunk, offset, length);
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
 * Convert ArrayBuffer to string
 *
 * @param      {Object}  buf     The buffer
 * @param      {Number}  offset  The offset
 * @param      {Number}  length  The length
 * @return     {String}
 */
export function bufferToString(buf, offset, length) {
  if(typeof buf == 'string') return buf;
  return misc.toString(buf, offset, length);
}

export function accessSync(pathname, mode) {
  if(misc.access(pathname, mode)) {
    errno = misc.error().errno;
    return -1;
  }

  return 0;
}

export function fopenSync(filename, flags = 'r', mode = 0o644) {
  let res = { errno: 0 };
  let file = std.open(filename, flags, res);

  if(!res.errno) return file;

  return numerr(-res.errno);
}

export function fdopenSync(fd, flags = 'r') {
  let res = { errno: 0 };
  let file = std.fdopenSync(fd, flags, res);

  if(!res.errno) return file;

  return numerr(-res.errno);
}

export function openSync(filename, flags = 'r', mode = 0o644) {
  if(typeof flags == 'number') return numerr(os.open(filename, flags, mode));

  return fopenSync(filename, flags, mode);
  //return numerr(std.open(filename, flags));
}

/* XXX: non-standard */
export function puts(fd, str) {
  if(typeof fd == 'object' && fd && typeof fd.puts == 'function') {
    fd.puts(str);
    fd.flush();
  } else {
    let data = misc.toArrayBuffer(str);
    return os.write(fd, data, 0, data.byteLength);
  }
}

/* XXX: non-standard */
export function gets(fd, str) {
  if(typeof fd == 'object' && fd && typeof fd.getline == 'function') return fd.getline();
}

export function flushSync(file) {
  if(typeof file != 'number') return file.flush();
}

export function seek(fd, offset, whence) {
  let ret;

  switch (typeof fd) {
    case 'number':
      ret = os.seek(fd, offset, whence);
      break;
    default:
      if(numerr(fd.seek(offset, whence)) == 0) ret = typeof offset == 'bigint' ? fd.tello() : fd.tell();
      break;
  }

  return ret;
}

export function tell(file) {
  switch (typeof file) {
    case 'number':
      return numerr(os.seek(file, 0, std.SEEK_CUR));
    default:
      return file.tell();
  }
}

export function sizeSync(file) {
  let bytes;
  const fd = typeof file == 'number' ? file : openSync(file, 'r');
  const pos = tell(fd);
  if((bytes = seek(fd, 0, std.SEEK_END)) < 0) return numerr(bytes);
  if(file !== fd) closeSync(fd);

  return bytes;
}

export function nameSync(file) {
  const fd = typeof file == 'number' ? file : fileno(file);
  let ret = readlinkSync(`/proc/self/fd/` + fd);

  if(typeof ret == 'string') return ret;

  let size = Math.max(1024, sizeSync(fd));
  let map = mmap(0, size, undefined, undefined, fd, 0);
  let name = filename(map);
  munmap(map, size);

  return name;
}

export function getcwd() {
  return strerr(os.getcwd());
}

export function chdir(path) {
  return numerr(os.chdir(path));
}

export function isatty(file) {
  const fd = fileno(file);
  return os.isatty(fd);
}

export function fileno(file) {
  return { number: f=> f,  object: f => (f && typeof f.fileno == 'function') ? f.fileno() : undefined  }[typeof file](file);
  /*if(typeof file == 'number') return file;
  if(typeof file == 'object' && file != null && typeof file.fileno == 'function') return file.fileno();*/
}

export function readFileSync(file, options = {}) {
  options = typeof options == 'string' ? { encoding: options } : options;
  options ??= {};

  if(options.encoding == 'utf-8') return std.loadFile(file);

  let data,
    size,
    res = { errno: 0 },
    f = std.open(file, 'r', res);

  if(!res.errno) {
    f.seek(0, std.SEEK_END);
    size = f.tell();
    if(typeof size == 'number') {
      f.seek(0, std.SEEK_SET);
      data = new ArrayBuffer(size);
      f.read(data, 0, size);
      f.close();
      if(options.encoding != null) data = misc.toString(data);
      return data;
    }
  }

  return numerr(-res.errno);
}

export function writeFileSync(file, data, overwrite = true) {
  let buf,
    bytes,
    res = { errno: 0 };

  if(typeof data == 'string') {
    let f = std.open(file, 'wb', res);
    if(!res.errno) {
      f.puts(data);
      f.flush();
      bytes = f.tell();
      return bytes;
    }

    return numerr(-res.errno);
  } else {
    const fd = os.open(file, O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL), 0o644);
    if(fd >= 0) {
      buf = typeof data == 'string' ? misc.toArrayBuffer(data) : data;
      let arr = new Uint8Array(buf);
      if(arr[arr.length - 1] == 0) buf = buf.slice(0, -1);
      bytes = writeSync(fd, buf, 0, buf.byteLength);
      closeSync(fd);
      return bytes;
    }
  }

  return fd;
}

export function closeSync(fd) {
  return numerr(typeof fd == 'number' ? os.close(fd) : fd.close());
}

export function existsSync(path) {
  let [st, err] = os.stat(path);
  return !err;
}

export function lstatSync(path) {
  let [st, err] = os.lstat(path);
  return err ? strerr([st, err]) : new Stats(st);
}

export function mkdirSync(path, mode = 0o777) {
  return numerr(os.mkdir(path, mode));
}

export function readSync(fd, buf, offset, length) {
  let ret;
  offset = offset || 0;
  length = length || buf.byteLength - offset;

  switch (typeof fd) {
    case 'number':
      ret = os.read(fd, buf, offset, length);
      break;
    default:
      ret = fd.read(buf, offset, length);
      break;
  }

  return numerr(ret);
}

export function readdirSync(path) {
  return strerr(os.readdir(path));
}

export function readlinkSync(path) {
  return strerr(os.readlink(path));
}

export function realpathSync(path) {
  return strerr(os.realpath(path));
}

export function renameSync(oldname, newname) {
  return numerr(os.rename(oldname, newname));
}

export function statSync(path) {
  let [st, err] = os.stat(path);
  return err ? strerr([st, err]) : new Stats(st);
}

export function symlinkSync(target, path) {
  return numerr(os.symlink(target, path));
}

export function tmpfileSync() {
  return objerr(std.tmpfile);
}

export function mkstempSync(template) {
  return numerr(misc.mkstemp(template));
}

export function tempnamSync(dir, pfx) {
  let base = process.argv[1] ?? process.argv[0];
  pfx ??= basename(base, extname(base)) + '-';

  return misc.tempnam(dir, pfx);
}

export function unlinkSync(path) {
  return numerr(os.remove(path));
}

export function writeSync(fd, data, offset, length) {
  if(!(data instanceof ArrayBuffer)) data = misc.toArrayBuffer(data);

  offset ??= 0;
  if(data && data.byteLength) length ??= data.byteLength;

  let ret;
  switch (typeof fd) {
    case 'number':
      ret = os.write(fd, data, offset, length);
      break;
    case 'object':
      ret = fd.write(data, offset, length);
      break;
    default:
      throw new Error(`invalid fd: ${fd}`);
  }

  return numerr(ret);
}

export function pipe() {
  let [rd, wr] = os.pipe();
  return [rd, wr];
}

/*export function setReadHandler(file, handler) {
  const fd = fileno(file);
  return os.setReadHandler(fd, handler);
}

export function setWriteHandler(file, handler) {
  const fd = fileno(file);
  return os.setWriteHandler(fd, handler);
}*/

export function onRead(file, handler = null) {
  const fd = fileno(file);
  os.setReadHandler(fd, handler);
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
  const fd = fileno(file);
  os.setWriteHandler(fd, handler);
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
  const buf = typeof bufferOrBufSize == 'number' ? buffer(bufferOrBufSize) : bufferOrBufSize;
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
      }
    })
  };
}

export function* readerSync(input, bufferOrBufSize = 1024) {
  const buf = typeof bufferOrBufSize == 'number' ? buffer(bufferOrBufSize) : bufferOrBufSize;
  let ret;

  while((ret = readSync(input, buf, 0, buf.byteLength)) > 0) yield buf.slice(0, ret);

  return closeSync(input);
}

export function readAllSync(input, bufSize = 1024) {
  let s = '';
  for(let chunk of readerSync(input, bufSize)) s += misc.toString(chunk);
  return s;
}

export async function open(filename, flags = 'file', mode = 0o644) {
  const errorObj = { errno: 0 };
  const file = std.open(filename, flags, errorObj);

  if(file == null || errorObj.errno) {
    throw MakeError(`Error opening '${filename}' (${flags})`, -errorObj.errno, filename, 'open');
  }

  return new FileHandle(file);
}

export async function flush(file) {
  const fd = fileno(file);

  await waitWrite(fd);

  return flushSync(file);
}

export async function close(fd) {
  return closeSync(fd);
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
  let ret;
  do {
    await waitRead(fd);
    errno = 0;
    ret = readSync(fd, buf, offset, length);
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
  let ret;
  do {
    await waitWrite(fd);
    errno = 0;
    ret = writeSync(fd, buf, offset, length);
  } while(ret == -1 && errno == EWOULDBLOCK);

  return ret;
}

export async function readAll(input, bufSize = 1024) {
  let s = '';
  for await(let chunk of reader(input, bufSize)) s += misc.toString(chunk);
  return s;
}

class inotify_event extends ArrayBuffer {
  constructor(obj = {}) {
    super(24);
    Object.assign(this, obj);
  }

  /* 0: int wd */
  set wd(value) {
    if(typeof value == 'object' && value != null && value instanceof ArrayBuffer) value = toPointer(value);
    new Int32Array(this, 0)[0] = value;
  }
  get wd() {
    return new Int32Array(this, 0)[0];
  }

  /* 4: uint32_t (unsigned int) mask */
  set mask(value) {
    if(typeof value == 'object' && value != null && value instanceof ArrayBuffer) value = toPointer(value);
    new Uint32Array(this, 4)[0] = value;
  }
  get mask() {
    return new Uint32Array(this, 4)[0];
  }

  /* 8: uint32_t (unsigned int) cookie */
  set cookie(value) {
    if(typeof value == 'object' && value != null && value instanceof ArrayBuffer) value = toPointer(value);
    new Uint32Array(this, 8)[0] = value;
  }
  get cookie() {
    return new Uint32Array(this, 8)[0];
  }

  /* 12: uint32_t (unsigned int) len */
  set len(value) {
    if(typeof value == 'object' && value != null && value instanceof ArrayBuffer) value = toPointer(value);
    new Uint32Array(this, 12)[0] = value;
  }
  get len() {
    return new Uint32Array(this, 12)[0];
  }

  /* 16: char [] name */
  set name(value) {
    if(typeof value == 'object' && value != null && value instanceof ArrayBuffer) value = toPointer(value);
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

  if(typeof options == 'string') options = { encoding: options };
  if(typeof options == 'number') options = { mask: options };

  ret = new EventEmitter();

  ret.on('change', filename => callback('change', filename));
  ret.on('rename', filename => callback('rename', filename));

  options.mask ??= misc.IN_MODIFY | misc.IN_MOVE_SELF | misc.IN_MOVED_TO | misc.IN_DELETE | misc.IN_DELETE_SELF | misc.IN_CREATE | misc.IN_CLOSE_WRITE | misc.IN_ATTRIB;

  try {
    fd = misc.watch();
    wd = misc.watch(fd, filename, options.mask);
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

      name = misc.toString(buf.slice(0, r));
    }

    let event = {
      wd,
      mask,
      cookie,
      name
    };

    //std.puts(`readHandler r=${r}\n`);

    ret.emit('change', filename);
  });

  //std.puts(`watch ret=${ret}\n`);

  return ret;
}

export function createReadStream(path, options = { encoding: 'utf8' }) {
  const { encoding, bufSize = 1024, start = 0, end = Infinity, flags = 'r' } = typeof options == 'string' ? { encoding: options } : options;
  let file,
    err = {},
    buf,
    decoder,
    bytesRead = 0;
  if(encoding) decoder = new TextDecoder(encoding);
  return new ReadableStream({
    start(controller) {
      file = std.open(path, flags + 'b', err);

      if(start > 0) file.seek(start, std.SEEK_SET);

      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
    },
    pull(controller) {
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
      } else if(file.error()) {
        controller.error(file);
      } else {
        controller.close();
      }
    },
    cancel(reason) {
      file.close();
    }
  });
}

export function createWriteStream(path, options = { encoding: 'utf8' }) {
  let { encoding, start = 0, flags } = typeof options == 'string' ? { encoding: options } : options;
  let file,
    err = {},
    bytesWritten = 0,
    encoder;
  flags ??= start > 0 ? 'r+' : 'w';
  return new WritableStream({
    start(controller) {
      file = std.open(path, flags + 'b', err);
      if(start > 0) file.seek(start, std.SEEK_SET);
      if(err.errno) throw new Error("Error opening '" + path + "': " + std.strerror(err.errno));
    },
    write(chunk, controller) {
      if(typeof chunk == 'string') {
        encoder ??= new TextEncoder(encoding);
        chunk = encoder.encode(chunk).buffer;
      }
      const ret = file.write(chunk, 0, chunk.byteLength);
      if(ret > 0) bytesWritten += ret;
      if(file.error()) controller.error(file);
    },
    close(controller) {
      file.close();
    },
    abort(reason) {
      file.close();
    }
  });
}

const CharWidth = {
  1: Uint8Array,
  2: Uint16Array,
  4: Uint32Array
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
  flush,
  flushSync,
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
  setReadHandler,
  setWriteHandler,
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
  writeSync
};
