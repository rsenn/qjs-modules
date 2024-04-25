import { closeSync, flushSync, nameSync, readAllSync, readSync, readerSync, writeSync, readAll, read, reader, write } from 'fs';
import { join, absolute, relative, normalize, resolve, slice, length, isRelative, isAbsolute } from 'path';
import { define, keys } from 'util';
import { ArrayPrototype, ArrayExtensions } from 'extendArray';
import * as fs from 'fs';
import { Archive } from 'archive';

export class VFSInterface {}

function contains(dir, entry) {
  dir = resolve(dir);
  entry = resolve(entry);
  const len = length(dir);
  return dir == slice(entry, 0, len);
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

  readFileSync(p, ...args) {
    const path = this.#checkPath(p, true, true);
    return this.#baseImpl(path).readFileSync(path, ...args);
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

define(UnionFS.prototype, {
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
});

define(
  VFSInterface.prototype,
  keys(UnionFS.prototype).reduce((o, n) => {
    o[n] = function() {};
    return o;
  }, {})
);

export class ArchiveFS extends VFSInterface {
  #archive = null;

  constructor(ar) {
    this.#archive = ar;
  }
}
