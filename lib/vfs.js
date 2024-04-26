import { closeSync, flushSync, nameSync, readAllSync, readSync, readerSync, writeSync, readAll, read, reader, write } from 'fs';
import { join, absolute, relative, normalize, resolve, slice, length, isRelative, isAbsolute } from 'path';
import { define, keys, toString, charLength, nonenumerable } from 'util';
import { ArrayPrototype, ArrayExtensions } from 'extendArray';
import * as fs from 'fs';
import { Archive, ArchiveEntry } from 'archive';
import { IOReadDecorator, IOWriteDecorator } from 'io';

export class VFSInterface {}

function contains(dir, entry, res = true) {
  if(res) {
    dir = resolve(dir);
    entry = resolve(entry);
  }
  const len = length(dir);
  return length(entry) > len && dir == slice(entry, 0, len);
}

export class UnionFS extends VFSInterface {
  #paths = [];
  #impl = [];

  constructor() {
    super();
  }

  appendPath(p, impl = fs) {
    if(ArrayExtensions.pushUnique.call(this.#paths, resolve(p))) {
      this.#impl.push(impl);
      return true;
    }
  }

  prependPath(p, impl = fs) {
    if(ArrayExtensions.unshiftUnique.call(this.#paths, resolve(p))) {
      this.#impl.unshift(impl);
      return true;
    }
  }

  removePath(p) {
    let i, dir, fs;

    while((i = ArrayPrototype.indexOf.call(this.#paths, resolve(p))) != -1) {
      [dir] = this.#paths.splice(i, i + 1);
      [fs] = this.#impl.splice(i, i + 1);
    }

    return [dir, fs];
  }

  hasPath(p) {
    return ArrayPrototype.indexOf.call(this.#paths, resolve(p)) != -1;
  }

  /* prettier-ignore */ get pathArray() { return this.#paths; }
  /* prettier-ignore */ get implArray() { return this.#impl; }

  #checkPath(path, mustExist = false, doThrow = false) {
    if(path == '.') return this.#paths[0] ?? path;

    if(isAbsolute(path)) {
      let i = 0;

      for(let p of this.#paths) {
        if(contains(p, path)) return mustExist && !this.#impl[i].existsSync(path) ? null : resolve(path);
        ++i;
      }
    } else {
      let i = 0;

      for(let p of this.#paths) {
        if(this.#impl[i].existsSync(join(p, path))) return resolve(join(p, path));
        ++i;
      }

      let p2 = resolve(path);
      if(p2 != path) return this.#checkPath(p2, mustExist, doThrow);
    }

    if(!mustExist) if (this.#paths[0]) if (isRelative(path)) return join(this.#paths[0], path);

    if(doThrow) throw new Error(`VFS path '${path}' invalid`);

    return null;
  }

  #baseIndex(path) {
    let i = 0;

    if(isRelative(path)) path = absolute(path);

    for(let p of this.#paths) {
      if(contains(p, path)) break;
      ++i;
    }

    if(i >= this.#paths.length) i = -1;
    return i;
  }

  #baseImpl(path) {
    return this.#impl[this.#baseIndex(path)];
  }

  accessSync(p, mode) {
    const path = this.#checkPath(p, false, true);
    return this.#baseImpl(path).accessSync(path, mode);
  }

  existsSync(p) {
    const path = this.#checkPath(p, false, true);
    return this.#baseImpl(path).existsSync(path);
  }

  statSync(p, st) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).statSync(path, st);
  }

  lstatSync(p, st) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).lstatSync(path, st);
  }

  mkdirSync(p, mode = 0x1ed) {
    const path = this.#checkPath(p);
    return this.#baseImpl(path).mkdirSync(path, mode);
  }

  mkstempSync(p) {
    const path = this.#checkPath(p);
    return this.#baseImpl(path).mkstempSync(path);
  }

  openSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).openSync(path, ...args);
  }

  readFileSync(p, options = {}) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).readFileSync(path, options);
  }

  readdirSync(dir) {
    const path = this.#checkPath(dir, true, true);

    if(isAbsolute(path) && dir != '.')
      return this.#baseImpl(path)
        .readdirSync(path)
        .map(n => join(dir, n));

    return this.#paths.reduce(
      (r, d) =>
        r.concat(
          this.#baseImpl(d)
            .readdirSync(d)
            .map(n => join(d, n))
        ),
      []
    );
  }

  readlinkSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).readlinkSync(path, ...args);
  }

  realpathSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).realpathSync(path, ...args);
  }

  renameSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).renameSync(path, ...args);
  }

  sizeSync(p) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).sizeSync(path);
  }

  symlinkSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).symlinkSync(path, ...args);
  }

  tempnamSync(dir, pfx) {
    const path = this.#checkPath(dir, false, true);
    return this.#baseImpl(path).tempnamSync(path, pfx);
  }

  unlinkSync(p) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).unlinkSync(path);
  }

  writeFileSync(p, ...args) {
    const path = this.#checkPath(p, false, true);
    return this.#baseImpl(path).writeFileSync(path, ...args);
  }

  tmpfileSync() {
    const path = this.#paths[0] ?? '.';
    const name = this.#baseImpl(path).tempnamSync(path);

    return this.#impl.openSync(name, 'w+');
  }
}

define(
  UnionFS.prototype,
  nonenumerable({
    closeSync,
    flushSync,
    nameSync,
    readAllSync,
    readSync,
    readerSync,
    writeSync,
    readAll,
    read,
    reader,
    write
  })
);

define(
  VFSInterface.prototype,
  nonenumerable(
    keys(UnionFS.prototype)
      .filter(n => !/(Path|Array)$/.test(n))
      .reduce((o, n) => {
        o[n] = () => {
          throw new Error(`VFSInterface.${n}() unimplemented`);
        };
        return o;
      }, {})
  )
);

const IODecorator = define({ [Symbol.toStringTag]: 'IOStream' }, IOReadDecorator, IOWriteDecorator);

export class ArchiveFS {
  #archive = null;
  #mode = undefined;

  constructor(ar, rw) {
    if(typeof ar == 'string') {
      this.#archive = rw ? Archive.write(ar) : ar;
      this.#mode = rw;
    } else {
      const { file, mode } = ar;

      this.#archive = rw ?? mode ? ar : file;
      this.#mode = rw ?? mode;
    }
  }

  get archive() {
    if(typeof this.#archive == 'object') return this.#archive;
  }

  readdirSync(dir) {
    if(this.#mode != Archive.READ) throw new Error(`archive is not in read mode`);

    const a = [],
      instance = new Archive();

    for(let entry of instance.open(this.#archive)) if(contains(dir, entry.pathname)) a.push(entry.pathname);

    return a;
  }

  #find(path) {
    const instance = new Archive().open(this.#archive);
    let entry;

    while((entry = instance.next())) if(entry.pathname == path) return [instance, entry];
    return [,];
  }

  existsSync(path) {
    const [ar, ent] = this.#find(path);
    ar?.close();
    return !!ent;
  }

  sizeSync(path) {
    const [ar, ent] = this.#find(path);
    ar?.close();
    return ent?.size;
  }

  statSync(path) {
    const [ar, ent] = this.#find(path);
    ar?.close();
    return ent;
  }

  lstatSync(path) {
    const [ar, ent] = this.#find(path);
    ar?.close();
    return ent;
  }

  readFileSync(path, options = {}) {
    if(this.#mode != Archive.READ) throw new Error(`archive is not in read mode`);

    let b;
    options = typeof options == 'string' ? { encoding: options } : options;
    const [ar, entry] = this.#find(path);

    if(entry) {
      const { size, pathname } = entry;
      b = new ArrayBuffer(size);
      let r = ar.read(b);
      if(options.encoding == 'utf-8') b = toString(b, 0, r);
    }

    ar?.close();
    return b;
  }

  openSync(path) {
    let r,
      pos = 0;

    if(this.#mode != Archive.READ) {
      const ar = this.#archive;

      let entry = new ArchiveEntry(path, { mode: 0o644, type: 'file' });

      ar.write(entry);

      return Object.setPrototypeOf(
        nonenumerable({
          write: (b, ofs, len) => {
            r = ar.write(b, ofs ?? 0, len ?? b.byteLength ?? b.length);
            if(r > 0) pos += r;
            return r;
          },
          tell: () => pos,
          error: () => ar.error != null,
          close: () => ar.write(),
          [Symbol.toStringTag]: 'WriteStream'
        }),
        IOWriteDecorator
      );
    } else {
      const [ar, entry] = this.#find(path);

      const { size } = entry;

      if(entry) {
        return Object.setPrototypeOf(
          nonenumerable({
            size,
            read: (b, ofs, len) => {
              r = ar.read(b, ofs ?? 0, len ?? b.byteLength);
              if(r > 0) pos += r;
              return r;
            },
            eof() {
              return r === 0 || this.tell() == this.size;
            },
            tell: () => pos,
            error: () => ar.error != null,
            close: () => ar.close(),
            [Symbol.toStringTag]: 'ReadStream'
          }),
          IOReadDecorator
        );
      }
    }
  }
}
