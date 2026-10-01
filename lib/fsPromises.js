import { accessSync, appendFileSync, bufferArguments, buffer, chmodSync, chownSync, closeSync, constants, copyFileSync, cpSync, FileHandle, InvalidBuffer, lchownSync, linkSync, lstatSync, lutimesSync, mkdirSync, mkdtempSync, opendirSync, openSync, readdirSync, readFileSync, readlinkSync, readSync, realpathSync, renameSync, rmdirSync, rmSync, statSync, stringOrBufferArguments, symlinkSync, throwIfNull, truncateSync, unlinkSync, utimesSync, waitRead, waitWrite, watch as watchSync, writeFileSync, writeSync, } from 'fs';
import { isNumber } from 'misc';
import { TextDecoder } from 'textcode';

/* readSync()/writeSync() throw (via syscallerr()) rather than returning -1 with
 * errno set, so this retry never actually fires today - kept to match fs.js's
 * pre-move behavior for read()/write() rather than silently dropping it. */
let errno = 0;
const EAGAIN = 11;
const EWOULDBLOCK = 11;

export async function access(pathname, mode) {
  return accessSync(pathname, mode);
}

export async function appendFile(path, data, options) {
  return appendFileSync(path, data, options);
}

export async function chmod(path, mode) {
  return chmodSync(path, mode);
}

export async function chown(path, uid, gid) {
  return chownSync(path, uid, gid);
}

export async function copyFile(src, dest, mode) {
  return copyFileSync(src, dest, mode);
}

export async function cp(src, dest, options) {
  return cpSync(src, dest, options);
}

export { constants };

/* fsPromises.lchmod() is macOS-only in Node (and deprecated) - not implemented here either. */

export async function lchown(path, uid, gid) {
  return lchownSync(path, uid, gid);
}

export async function link(existingPath, newPath) {
  return linkSync(existingPath, newPath);
}

export async function lstat(path, options) {
  return lstatSync(path);
}

export async function lutimes(path, atime, mtime) {
  return lutimesSync(path, atime, mtime);
}

export async function mkdir(path, options) {
  return mkdirSync(path, options);
}

export async function mkdtemp(prefix, options) {
  return mkdtempSync(prefix);
}

export async function mkdtempDisposable(prefix, options) {
  const path = mkdtempSync(prefix);
  const remove = async () => rmSync(path, { recursive: true, force: true });

  return { path, remove, [Symbol.asyncDispose]: remove };
}

class PromiseFileHandle {
  #handle;

  constructor(handle) {
    this.#handle = handle;
  }

  get fd() {
    return this.#handle.fd;
  }

  async appendFile(...args) {
    return this.#handle.appendFile(...args);
  }

  async chmod(...args) {
    return this.#handle.chmod(...args);
  }

  async chown(...args) {
    return this.#handle.chown(...args);
  }

  async close() {
    return this.#handle.close();
  }

  async datasync() {
    return this.#handle.datasync();
  }

  async read(...args) {
    return this.#handle.read(...args);
  }

  async readFile(...args) {
    return this.#handle.readFile(...args);
  }

  async *readLines() {
    const lines = (await this.#handle.readFile('utf8')).split(/\r?\n/);

    if(lines[lines.length - 1] === '') lines.pop();

    yield* lines;
  }

  async readv(buffers, position) {
    let bytesRead = 0;

    for(const buffer of buffers) {
      const at = position == null ? undefined : position + bytesRead;
      const read = this.#handle.read(buffer, 0, buffer.byteLength, at).bytesRead;

      bytesRead += read;

      if(read < buffer.byteLength) break;
    }

    return { bytesRead, buffers };
  }

  async writev(buffers, position) {
    let bytesWritten = 0;

    for(const buffer of buffers) {
      const at = position == null ? undefined : position + bytesWritten;

      bytesWritten += this.#handle.write(buffer, 0, buffer.byteLength, at).bytesWritten;
    }

    return { bytesWritten, buffers };
  }

  async [Symbol.asyncDispose]() {
    return this.#handle.close();
  }

  async stat(...args) {
    return this.#handle.stat(...args);
  }

  async sync() {
    return this.#handle.sync();
  }

  async truncate(...args) {
    return this.#handle.truncate(...args);
  }

  async utimes(...args) {
    return this.#handle.utimes(...args);
  }

  async write(...args) {
    return this.#handle.write(...args);
  }

  async writeFile(...args) {
    return this.#handle.writeFile(...args);
  }
}

export async function open(filename, flags = 'r', mode = 0o644) {
  return new PromiseFileHandle(new FileHandle(openSync(filename, flags, mode)));
}

export async function opendir(path, options) {
  return opendirSync(path);
}

export async function readdir(path, options) {
  return readdirSync(path, options);
}

export async function readFile(file, options = {}) {
  return readFileSync(file, options);
}

export async function readlink(path) {
  return readlinkSync(path);
}

export async function realpath(path) {
  return realpathSync(path);
}

export async function rename(oldname, newname) {
  return renameSync(oldname, newname);
}

export async function rm(path, options) {
  return rmSync(path, options);
}

export async function rmdir(path, options) {
  return rmdirSync(path);
}

export async function stat(path, options) {
  return statSync(path);
}

export async function symlink(target, path, type) {
  return symlinkSync(target, path);
}

export async function truncate(path, len) {
  return truncateSync(path, len);
}

export async function unlink(path) {
  return unlinkSync(path);
}

export async function utimes(path, atime, mtime) {
  return utimesSync(path, atime, mtime);
}

/**
 * Node-compatible fsPromises.watch(): an async iterable of
 * {eventType, filename}, backed by the same fs.watch()/FSWatcher.
 */
export async function* watch(filename, options = {}) {
  const queue = [];
  let wake = null;
  let closed = false;

  const watcher = watchSync(filename, options, (eventType, filename) => {
    const item = { eventType, filename };
    if(wake) {
      const resolve = wake;
      wake = null;
      resolve(item);
    } else {
      queue.push(item);
    }
  });

  watcher.on('close', () => {
    closed = true;
    if(wake) {
      const resolve = wake;
      wake = null;
      resolve(null);
    }
  });

  try {
    for(;;) {
      if(queue.length) {
        yield queue.shift();
        continue;
      }

      if(closed) return;

      const item = await new Promise(resolve => (wake = resolve));

      if(item === null) return;

      yield item;
    }
  } finally {
    watcher.close();
  }
}

export async function writeFile(file, data, options) {
  return writeFileSync(file, data, options);
}

/* Non-standard: not part of Node/Bun's fs/promises API (which has no top-level
 * raw-fd read()/write()/reader() - only FileHandle methods). Kept here for
 * lib/vfs.js's UnionFS.prototype, which mixes these in alongside the *Sync
 * equivalents. */
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

/* Non-standard, see read() above. */
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

/* Non-standard, see read() above. */
export function reader(input, bufferOrBufSize = 1024) {
  const buf = isNumber(bufferOrBufSize) ? buffer(bufferOrBufSize) : bufferOrBufSize;

  return {
    [Symbol.asyncIterator]: () => ({
      async next(numBytes = buf.byteLength) {
        let ret,
          received = 0;

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

/* Non-standard, see read() above. */
export async function readAll(input, bufSize = 1024) {
  let s = '';
  const dec = new TextDecoder('utf-8');
  for await(const chunk of reader(input, bufSize)) s += dec.decode(chunk);
  return s;
}
