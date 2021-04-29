import * as std from 'std';
import * as os from 'os';
import * as path from 'path';

let errno = 0;
const { O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_EXCL, O_TRUNC, O_TEXT } = os;
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

const fs = {
  O_RDONLY,
  O_WRONLY,
  O_RDWR,
  O_APPEND,
  O_CREAT,
  O_EXCL,
  O_TRUNC,
  O_TEXT,
  get errno() {
      return errno;
    },
  get errstr() {
      return std.strerror(errno);
    },
  Stats: class Stats {
    constructor(st) {
      this.mode = st.mode;

      //  if(Object.hasOwnProperty(st, prop))
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
  },
  buffer(bytes) {
    return CreateArrayBuffer(bytes);
  },
  bufferFrom(chunk, encoding = 'utf-8') {
    return StringToArrayBuffer(chunk, 1);
  },
  bufferSize(buf) {
    return ArrayBufferSize(buf);
  },
  bufferToString(buf, n = 1) {
    if(typeof buf == 'string') return buf;
    return ArrayBufferToString(buf, n);
  },
  fopen(filename, flags = 'r', mode = 0o644) {
    let res = { errno: 0 };
    let file = std.open(filename, flags, res);
    if(!res.errno) return file;

    return numerr(-res.errno);
  },

  fdopen(fd, flags = 'r') {
    let res = { errno: 0 };
    let file = std.fdopen(fd, flags, res);
    if(!res.errno) return file;

    return numerr(-res.errno);
  },

  readFileSync,
  writeFileSync,
  exists,
  lstat,
  mkdir,
  open,
  read,
  readdir,
  readlink,
  realpath,
  rename,
  stat,
  symlink,
  unlink,
  write,
  puts(fd, str) {
    if(typeof fd == 'object' && fd && typeof fd.puts == 'function') {
      //  console.log("puts", {fd,str});
      fd.puts(str);
      fd.flush();
    } else {
      let data = StringToArrayBuffer(str);
      return this.write(fd, data, 0, data.byteLength);
    }
  },
  flush(file) {
    if(typeof file != 'number') return file.flush();
  },

  seek(fd, offset, whence) {
    let ret;
    switch (typeof fd) {
      case 'number':
        ret = os.seek(fd, offset, whence);
        break;
      default: if (numerr(fd.seek(offset, whence)) == 0)
          ret = typeof offset == 'bigint' ? fd.tello() : fd.tell();
        break;
    }
    console.log('seek:', { offset, whence, ret });
    return ret;
  },
  tell(file) {
    switch (typeof file) {
      case 'number':
        return numerr(os.seek(file, 0, std.SEEK_CUR));
      default: return file.tell();
    }
  },
  size(file) {
    let bytes, pos;
    let fd = typeof file == 'number' ? file : this.open(file, 'r');
    pos = this.tell(fd);
    bytes = this.seek(fd, 0, std.SEEK_END);
    if(file !== fd) this.close(fd);
    return bytes;
  },

  getcwd() {
    return strerr(os.getcwd());
  },
  chdir(path) {
    return numerr(os.chdir(path));
  },

  isatty(file) {
    let fd = this.fileno(file);
    return os.isatty(fd);
  },

  fileno(file) {
    if(typeof file == 'number') return file;
    if(typeof file == 'object' && file != null && typeof file.fileno == 'function')
      return file.fileno();
  },
  get stdin() {
      return std.in;
    },
  get stdout() {
      return std.out;
    },
  get stderr() {
      return std.err;
    },
  pipe() {
    let [rd, wr] = os.pipe();
    return [rd, wr];
  },
  setReadHandler(file, handler) {
    let fd = typeof file == 'number' ? file : file.fileno();
    return os.setReadHandler(fd, handler);
  },
  setWriteHandler(file, handler) {
    let fd = typeof file == 'number' ? file : file.fileno();
    return os.setWriteHandler(fd, handler);
  },
  waitRead(file) {
    let fd = typeof file == 'number' ? file : file.fileno();
    return new Promise((resolve, reject) => {
      os.setReadHandler(fd, () => {
        os.setReadHandler(fd, null);
        resolve(file);
      });
    });
  },
  waitWrite(file) {
    let fd = typeof file == 'number' ? file : file.fileno();
    return new Promise((resolve, reject) => {
      os.setWriteHandler(fd, () => {
        os.setWriteHandler(fd, null);
        resolve(file);
      });
    });
  },
  readAll(input, bufSize = 1024) {
    const buffer = this.buffer(bufSize);
    let output = '';
    let ret;
    do {
      //await this.waitRead(input);
      ret = this.read(input, buffer, 0, bufSize);
      //console.log('readAll', { ret, input: this.fileno(input), buffer });
      let str = this.bufferToString(buffer.slice(0, ret));
      output += str;
      if(ret < bufSize) break;
    } while(ret > 0);
    return output;
  }
};

export function readFileSync(filename, encoding = 'utf-8') {
  if(encoding == 'utf-8') return std.loadFile(filename);

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
  if(typeof data == 'string') {
    let file = std.open(filename, 'w+b', res);
    if(!res.errno) {
      file.puts(data);
      file.flush();
      bytes = file.tell();
      file.close();
      return bytes;
    }
    return numerr(-res.errno);
  } else {
    let fd = fs.open(filename, O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL));
    if(fd >= 0) {
      buf = typeof data == 'string' ? StringToArrayBuffer(data) : data;
      let arr = new Uint8Array(buf);
      if(arr[arr.length - 1] == 0) buf = buf.slice(0, -1);
      bytes = fs.write(fd, buf, 0, buf.byteLength);
      fs.close(fd);
      return bytes;
    }
  }
  return fd;
}

export function close(fd) {
  return numerr(typeof fd == 'number' ? os.close(fd) : fd.close());
}

export function exists(path) {
  let file = std.open(path, 'r');
  let res = file != null;
  if(file) file.close();
  return res;
}
export function lstat(path, cb) {
  let [st, err] = os.lstat(path);
  return err ? strerr([st, err]) : new fs.Stats(st);
}

export function mkdir(path, mode = 0o777) {
  return numerr(os.mkdir(path, mode));
}

export function open(filename, flags = O_RDONLY, mode = 0x1a4) {
  return numerr(os.open(filename, flags, mode));
  /*  if(fd => 0) return fd;

      return fd;*/
}

export function read(fd, buf, offset, length) {
  let ret;
  offset = offset || 0;
  length = length || buf.byteLength - offset;

  switch (typeof fd) {
    case 'number':
      ret = os.read(fd, buf, offset, length);
      break;
    default: ret = fd.read(buf, offset, length);
      break;
  }
  //  console.log("ret:", ret);
  return numerr(ret);
}

export function readdir(path) {
  return strerr(os.readdir(path));
}

export function readlink(path) {
  return strerr(os.readlink(path));
}

export function realpath(path) {
  return strerr(os.realpath(path));
}

export function rename(oldname, newname) {
  return numerr(os.rename(oldname, newname));
}

export function stat(path, cb) {
  let [st, err] = os.stat(path);
  return err ? strerr([st, err]) : new fs.Stats(st);
}

export function symlink(target, path) {
  return numerr(os.symlink(target, path));
}

export function unlink(path) {
  return numerr(os.remove(path));
}

export function write(fd, data, offset, length) {
  if(!(data instanceof ArrayBuffer)) data = StringToArrayBuffer(data);

  offset ??= 0;
  length ??= data.byteLength;

  //console.log("filesystem.write", { data: fs.bufferToString(data), offset, length });

  let ret;
  switch (typeof fd) {
    case 'number':
      ret = os.write(fd, data, offset, length);
      break;
    default: ret = fd.write(data, offset, length);
      break;
  }
  return numerr(ret);
}

export default fs;