import * as std from 'std';
import * as os from 'os';
import * as path from 'path';
import { toString as ArrayBufferToString, toArrayBuffer as StringToArrayBuffer } from 'misc';
import * as misc from 'misc';
import * as mmap from 'mmap';
import { EventEmitter } from 'events';

let errno = 0;
const { O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_EXCL, O_TRUNC } = os;
const { EINVAL, EIO, EACCES, EEXIST, ENOSPC, ENOSYS, EBUSY, ENOENT, EPERM } = std.Error;

export const stdin = std.in;
export const stdout = std.out;
export const stderr = std.err;

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

/**
 * File stat() properties
 *
 * @class      Stats (name)
 */
export class Stats {
  constructor(st) {
    this.mode = st.mode;

    for(let prop in st) this[prop] = st[prop];
  }
  isDirectory() {
    return !!(this.mode & os.S_IFDIR);
  }
  isCharacterDevice() {
    return !!(this.mode & os.S_IFCHR);
  }
  isBlockDevice() {
    return !!(this.mode & os.S_IFBLK);
  }
  isFile() {
    return !!(this.mode & os.S_IFREG);
  }
  isFIFO() {
    return !!(this.mode & os.S_IFIFO);
  }
  isSymbolicLink() {
    return !!(this.mode & os.S_IFLNK);
  }
  isSocket() {
    return !!(this.mode & os.S_IFSOCK);
  }
}

const fs = {
  O_RDONLY,
  O_WRONLY,
  O_RDWR,
  O_APPEND,
  O_CREAT,
  O_EXCL,
  O_TRUNC,
  /* prettier-ignore */ get errno() {
      return errno;
    },
  /* prettier-ignore */ get errstr() {
      return std.strerror(errno);
    },
  Stats,
  buffer,
  bufferFrom,
  bufferSize,
  bufferToString,
  fopen,
  fdopen,
  readFileSync,
  writeFileSync,
  existsSync,
  lstatSync,
  mkdirSync,
  openSync,
  readSync,
  readdirSync,
  readlinkSync,
  realpathSync,
  renameSync,
  statSync,
  symlinkSync,
  unlinkSync,
  writeSync,
  closeSync,
  puts,
  flushSync,
  seek,
  tell,
  sizeSync,
  getcwd,
  chdir,
  isatty,
  fileno,
  stdin,
  stdout,
  stderr,
  pipe,
  setReadHandler,
  setWriteHandler,
  onRead,
  waitRead,
  onWrite,
  waitWrite,
  readAll
};

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
  return StringToArrayBuffer(chunk, offset, length);
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
  return ArrayBufferToString(buf, offset, length);
}

export function fopen(filename, flags = 'r', mode = 0o644) {
  let res = { errno: 0 };
  let file = std.open(filename, flags, res);
  if(!res.errno) return file;

  return numerr(-res.errno);
}

export function fdopen(fd, flags = 'r') {
  let res = { errno: 0 };
  let file = std.fdopen(fd, flags, res);
  if(!res.errno) return file;

  return numerr(-res.errno);
}

export function puts(fd, str) {
  if(typeof fd == 'object' && fd && typeof fd.puts == 'function') {
    //  console.log("puts", {fd,str});
    fd.puts(str);
    fd.flush();
  } else {
    let data = StringToArrayBuffer(str);
    return os.write(fd, data, 0, data.byteLength);
  }
}

export function flushSync(file) {
  //console.log("flushSync", file, fileno(file));

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
  //console.log('seek:', { offset, whence, ret });
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
  let bytes, pos;
  let fd = typeof file == 'number' ? file : openSync(file, 'r');
  pos = tell(fd);
  bytes = seek(fd, 0, std.SEEK_END);
  if(file !== fd) closeSync(fd);
  return bytes;
}

export function nameSync(file) {
  let fd = typeof file == 'number' ? file : fileno(file);
  let ret = readlinkSync(`/proc/self/fd/` + fd);

  if(typeof ret == 'string') return ret;

  let size = Math.max(1024, sizeSync(fd));
  let map = mmap.mmap(0, size, undefined, undefined, fd, 0);
  let name = mmap.filename(map);
  mmap.munmap(map, size);
  return name;
}

export function getcwd() {
  return strerr(os.getcwd());
}

export function chdir(path) {
  return numerr(os.chdir(path));
}

export function isatty(file) {
  let fd = fileno(file);
  return os.isatty(fd);
}

export function fileno(file) {
  if(typeof file == 'number') return file;
  if(typeof file == 'object' && file != null && typeof file.fileno == 'function') return file.fileno();
}

export function readFileSync(filename, options = {}) {
  options = typeof options == 'string' ? { encoding: options } : options;
  options ??= {};
  if(options.encoding == 'utf-8') return std.loadFile(filename);

  let data,
    size,
    res = { errno: 0 };
  let file = std.open(filename, 'r', res);
  if(!res.errno) {
    file.seek(0, std.SEEK_END);
    size = file.tell();
    file.seek(0, std.SEEK_SET);
    data = new ArrayBuffer(size);
    file.read(data, 0, size);
    //data = file.readAsString(/*size*/);
    file.close();
    if(options.encoding != null) data = ArrayBufferToString(data, options.encoding);
    return data;
  }
  return numerr(-res.errno);
}

export function writeFileSync(filename, data, overwrite = true) {
  let buf,
    bytes,
    res = { errno: 0 };
  //console.log('writeFileSync', { filename, data, overwrite });
  if(typeof data == 'string') {
    let file = std.open(filename, 'wb', res);
    if(!res.errno) {
      file.puts(data);
      file.flush();
      bytes = file.tell();
      file.close();
      return bytes;
    }
    return numerr(-res.errno);
  } else {
    let fd = os.open(filename, O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL), 0o644);
    if(fd >= 0) {
      buf = typeof data == 'string' ? StringToArrayBuffer(data) : data;
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

export function lstatSync(path, cb) {
  let [st, err] = os.lstat(path);
  return err ? strerr([st, err]) : new fs.Stats(st);
}

export function mkdirSync(path, mode = 0o777) {
  return numerr(os.mkdir(path, mode));
}

export function openSync(filename, flags = 'r', mode = 0o644) {
  if(typeof flags == 'number') return numerr(os.open(filename, flags, mode));
  return numerr(std.open(filename, flags));
}

export function read(fd, buf, offset, length) {
  return waitRead(fd).then(() => readSync(fd, buf, offset, length));
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
  //  console.log("ret:", ret);
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

export function statSync(path, cb) {
  let [st, err] = os.stat(path);
  return err ? strerr([st, err]) : new fs.Stats(st);
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
  pfx ??= path.basename(base, path.extname(base)) + '-';
  return misc.tempnam(dir, pfx);
}

export function unlinkSync(path) {
  return numerr(os.remove(path));
}

export function write(fd, buf, offset, length) {
  return waitWrite(fd).then(() => writeSync(fd, buf, offset, length));
}

export function writeSync(fd, data, offset, length) {
  if(!(data instanceof ArrayBuffer)) data = StringToArrayBuffer(data);

  offset ??= 0;
  if(data && data.byteLength) length ??= data.byteLength;
  //console.log('fs.writeSync', fd, data, offset, length);
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

export function setReadHandler(file, handler) {
  let fd = typeof file == 'number' ? file : file.fileno();
  return os.setReadHandler(fd, handler);
}

export function setWriteHandler(file, handler) {
  let fd = typeof file == 'number' ? file : file.fileno();
  return os.setWriteHandler(fd, handler);
}

export function onRead(file, handler = null) {
  let fd = typeof file == 'number' ? file : file.fileno();
  os.setReadHandler(fd, handler);
}

export function waitRead(file) {
  let fd = typeof file == 'number' ? file : file.fileno();
  return new Promise((resolve, reject) => {
    os.setReadHandler(fd, () => {
      os.setReadHandler(fd, null);
      resolve(file);
    });
  });
}

export function onWrite(file, handler = null) {
  let fd = typeof file == 'number' ? file : file.fileno();
  os.setWriteHandler(fd, handler);
}

export function waitWrite(file) {
  let fd = typeof file == 'number' ? file : file.fileno();
  return new Promise((resolve, reject) => {
    os.setWriteHandler(fd, () => {
      os.setWriteHandler(fd, null);
      resolve(file);
    });
  });
}

export async function readAll(input, bufSize = 1024) {
  const buf = buffer(bufSize);
  let output = '';
  let ret;
  do {
    ret = await read(input, buf, 0, bufSize);
    //console.log('readAll', { ret, input: fileno(input), buffer });
    let str = bufferToString(buf, 0, ret);
    output += str;
    if(ret < bufSize) break;
  } while(ret > 0);
  return output;
}

export function readAllSync(input, bufSize = 1024) {
  const buf = buffer(bufSize);
  let output = '';
  let ret;
  do {
    ret = readSync(input, buf, 0, bufSize);
    //console.log('readAll', { ret, input: fileno(input), buffer });
    let str = bufferToString(buf, 0, ret);
    output += str;
    if(ret < bufSize) break;
  } while(ret > 0);
  return output;
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

//inotify_event.prototype[Symbol.toStringTag] = 'inotify_event';

export function watch(filename, options = {}, callback = (eventType, filename) => {}) {
  let fd, wd, buf, arr, ret;

  if(typeof options == 'string') options = { encoding: options };
  if(typeof options == 'number') options = { mask: options };
  ret = new EventEmitter();

  ret.on('change', filename => callback('change', filename));
  ret.on('rename', filename => callback('rename', filename));

  options.mask ??=
    misc.IN_MODIFY |
    misc.IN_MOVE_SELF |
    misc.IN_MOVED_TO |
    misc.IN_DELETE |
    misc.IN_DELETE_SELF |
    misc.IN_CREATE |
    misc.IN_CLOSE_WRITE |
    misc.IN_ATTRIB;

  try {
    fd = misc.watch();
    //console.log(`fd=${fd}`);
    wd = misc.watch(fd, filename, options.mask);
    //console.log(`wd=${wd}`);
  } catch(err) {
    //console.log(`err=${err}`);
    return err;
  }

  buf = new ArrayBuffer(16);
  // evn = new inotify_event();
  arr = new Uint32Array(buf);

  os.setReadHandler(fd, () => {
    std.puts(`readHandler fd=${fd}\n`);
    let r = os.read(fd, buf, 0, buf.byteLength);

    //console.log('arr', console.config({ compact: 2 }), arr);
    const [wd, mask, cookie, len] = arr;
    let name = '';
    if(len > 0) {
      buf = new ArrayBuffer(len);
      r = os.read(fd, buf, 0, len);

      name = ArrayBufferToString(buf.slice(0, r));
    }

    let event = {
      wd,
      mask,
      cookie,
      name
    };
    //console.log('event', console.config({ compact: 2 }), event);

    std.puts(`readHandler r=${r}\n`);

    ret.emit('change', filename);
  });
  std.puts(`watch ret=${ret}\n`);

  return ret;
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

export default fs;
