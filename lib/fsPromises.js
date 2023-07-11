import { openSync, readSync, writeSync } from './lib/fs.js';

export function access(pathname, mode) {}
export function appendFile(path, data) {}
export function chmod(path, mode) {}
export function chown(path, uid, gid) {}
export function copyFile(src, dest, mode) {}
export function cp(src, dest, mode) {}
export function lchmod(path, mode) {}
export function lchown(path, uid, gid) {}
export function link(existingPath, newPath) {}
export function lstat(path, options) {}
export function lutimes(path, atime, mtime) {}
export function mkdir(path, mode = 0o777) {}
export function mkdtemp(prefix, options) {}

export async function open(filename, flags = 'r', mode = 0o644) {
  return openSync(filename, flags, mode);
}

export function opendir(path, options) {}
export function readdir(path) {}
export function readFile(file, options = {}) {}
export function readlink(path) {}
export function realpath(path) {}
export function rename(oldname, newname) {}
export function rm(path, options) {}
export function rmdir(path, options) {}
export function stat(path) {}
export function symlink(target, path) {}
export function truncate(path, len) {}
export function unlink(path) {}
export function utimes(path, atime, mtime) {}
export function watch(filename, options = {}, callback = (eventType, filename) => {}) {}
export function writeFile(file, data, overwrite = true) {}

export async function read(fd, buf, offset, length) {
  let ret;
  do {
    await waitRead(fd);
    errno = 0;
    ret = readSync(fd, buf, offset, length);
  } while(ret == -1 && errno == EAGAIN);

  return ret;
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

export async function write(fd, buf, offset, length) {
  let ret;
  do {
    await waitWrite(fd);
    errno = 0;
    ret = writeSync(fd, buf, offset, length);
  } while(ret == -1 && errno == EWOULDBLOCK);

  return ret;
}
