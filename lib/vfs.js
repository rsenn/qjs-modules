import { accessSync, closeSync, existsSync, flushSync, lstatSync, mkdirSync, mkstempSync, nameSync, openSync, readAllSync, readFileSync, readSync, readdirSync, readerSync, readlinkSync, realpathSync, renameSync, sizeSync, statSync, symlinkSync, tempnamSync, tmpfileSync, unlinkSync, writeFileSync, writeSync, readAll, read, reader, write } from 'fs';
import { join, relative, normalize, resolve } from 'path';
import { define } from 'util';

const methods = [
  'accessSync',
  'closeSync',
  'existsSync',
  'flushSync',
  'lstatSync',
  'mkdirSync',
  'mkstempSync',
  'nameSync',
  'openSync',
  'readAllSync',
  'readFileSync',
  'readSync',
  'readdirSync',
  'readerSync',
  'readlinkSync',
  'realpathSync',
  'renameSync',
  'sizeSync',
  'statSync',
  'symlinkSync',
  'tempnamSync',
  'tmpfileSync',
  'unlinkSync',
  'writeFileSync',
  'writeSync',
  'readAll',
  'read',
  'reader',
  'write'
];

export class Interface {}

define(
  Interface.prototype,
  methods.reduce((o, n) => {
    o[n] = function() {};
    return o;
  }, {})
);

export class UnionFS extends Interface {
  #paths = null;

  constructor(paths = []) {
    this.#paths = paths;
  }

  accessSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = accessSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  existsSync(path) {
    let r;

    for(let p of this.#paths) if((r = existsSync(join(p, path)))) break;

    return r;
  }

  statSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = statSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  lstatSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = lstatSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  mkdirSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = mkdirSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  mkstempSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = mkstempSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  openSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = openSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  readFileSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = readFileSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  readdirSync(dir) {
    let r;

    dir = relative(normalize(resolve('./lib/..')));

    if(dir == '.') return this.#paths.reduce((r, d) => r.concat(readdirSync(d)), []);

    for(let p of this.#paths) if((r = readdirSync(join(p, dir))) != null) break;

    return r;
  }

  readlinkSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = readlinkSync(join(p, path), ...args)) != null) break;

    return r;
  }

  realpathSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = realpathSync(join(p, path), ...args)) != null) break;

    return r;
  }

  renameSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = renameSync(join(p, path), ...args)) != -1) break;

    return r;
  }
  sizeSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = sizeSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  symlinkSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = symlinkSync(join(p, path), ...args)) != -1) break;

    return r;
  }

  tempnamSync(dir, pfx) {
    let r;

    for(let p of this.#paths) if((r = tempnamSync(join(p, dir), pfx))) break;

    return r;
  }

  tmpfileSync() {
    let name = this.tempnamSync('.');

    return this.openSync(name, 'w+');
  }

  unlinkSync(path) {
    let r;

    for(let p of this.#paths) if((r = unlinkSync(join(p, path))) != -1) break;

    return r;
  }

  writeFileSync(path, ...args) {
    let r;

    for(let p of this.#paths) if((r = writeFileSync(join(p, path), ...args)) != -1) break;

    return r;
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
