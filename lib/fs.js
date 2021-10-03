import * as std from 'std';
import * as os from 'os';
import * as path from 'path';
import { toString as ArrayBufferToString, toArrayBuffer as StringToArrayBuffer } from 'misc';

let errno = 0;
const { O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_EXCL, O_TRUNC } = os;
const { EINVAL, EIO, EACCES, EEXIST, ENOSPC, ENOSYS, EBUSY, ENOENT, EPERM } = std.Error;

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
  /* prettier-ignore */ get stdin() {
      return std.in;
    },
  /* prettier-ignore */ get stdout() {
      return std.out;
    },
  /* prettier-ignore */ get stderr() {
      return std.err;
    },
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
    if(encoding != null) data = ArrayBufferToString(data, encoding);
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
    let fd = openSync(filename, O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL));
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
  let file = std.open(path, 'r');
  let res = file != null;
  if(file) file.close();
  return res;
}

export function lstatSync(path, cb) {
  let [st, err] = os.lstat(path);
  return err ? strerr([st, err]) : new fs.Stats(st);
}

export function mkdirSync(path, mode = 0o777) {
  return numerr(os.mkdir(path, mode));
}

export function openSync(filename, flags = O_RDONLY, mode = 0x1a4) {
  //console.log('openSync', { filename, flags, mode });

  return numerr(std.open(filename, flags, mode));
  //  return numerr(os.open(filename, flags, mode));
  /*  if(fd => 0) return fd;

      return fd;*/
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

export function unlinkSync(path) {
  return numerr(os.remove(path));
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
  return new Promise((resolve, reject) => {
    onRead(file, () => {
      onRead(file, null);
      resolve(file);
    });
  });
}

export function onWrite(file, handler = null) {
  let fd = typeof file == 'number' ? file : file.fileno();
  os.setWriteHandler(fd, handler);
}

export function waitWrite(file) {
  return new Promise((resolve, reject) => {
    onWrite(file, () => {
      onWrite(file, null);
      resolve(file);
    });
  });
}

export function readAll(input, bufSize = 1024) {
  const buf = buffer(bufSize);
  let output = '';
  let ret;
  do {
    //await waitRead(input);
    ret = readSync(input, buf, 0, bufSize);
    //console.log('readAll', { ret, input: fileno(input), buffer });
    let str = bufferToString(buf, 0, ret);
    output += str;
    if(ret < bufSize) break;
  } while(ret > 0);
  return output;
}

const CharWidth = {
  1: Uint8Array,
  2: Uint16Array,
  4: Uint32Array
};

/*function ArrayBufferToString(buf, bytes = 1) {
  if(typeof bytes == 'string') bytes = Encoding2Bytes(bytes);
  let ctor = CharWidth[bytes];
  return String.fromCharCode(...new ctor(buf));
}
function StringToArrayBuffer(str, bytes = 1) {
  const buf = new ArrayBuffer(str.length * bytes);
  const view = new CharWidth[bytes](buf);
  for(let i = 0, strLen = str.length; i < strLen; i++) view[i] = str.codePointAt(i);
  return buf;
}*/

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
