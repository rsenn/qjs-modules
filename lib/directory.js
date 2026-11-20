import { readdirSync, readlinkSync } from 'node:fs';

/* Entry-shape flags */
const NAME = 1;
const TYPE = 2;
const BOTH = NAME | TYPE;
const FLAG_BUFFER = 0x80;

/* Entry types (match quickjs-directory.c / defines.h) */
const TYPE_REG = 1;
const TYPE_DIR = 2;
const TYPE_LNK = 4;
const TYPE_BLK = 8;
const TYPE_CHR = 16;
const TYPE_FIFO = 32;
const TYPE_SOCK = 64;
const TYPE_MASK = 127;

const STATICS = {
  NAME,
  TYPE,
  BOTH,
  TYPE_BLK: TYPE_BLK_(),
  TYPE_CHR: TYPE_CHR_(),
  TYPE_DIR,
  TYPE_FIFO: TYPE_FIFO_(),
  TYPE_LNK,
  TYPE_REG,
  TYPE_SOCK: TYPE_SOCK_(),
  TYPE_MASK,
};

function TYPE_BLK_() {
  return 8;
}
function TYPE_CHR_() {
  return 16;
}
function TYPE_FIFO_() {
  return 32;
}
function TYPE_SOCK_() {
  return 64;
}

/* Map a node fs Dirent to the numeric type bits used by the C module */
function direntType(dirent) {
  if(dirent.isBlockDevice && dirent.isBlockDevice()) return TYPE_BLK;
  if(dirent.isCharacterDevice && dirent.isCharacterDevice()) return TYPE_CHR;
  if(dirent.isDirectory()) return TYPE_DIR;
  if(dirent.isFIFO && dirent.isFIFO()) return TYPE_FIFO;
  if(dirent.isSymbolicLink()) return TYPE_LNK;
  if(dirent.isSocket && dirent.isSocket()) return TYPE_SOCK;
  if(dirent.isFile()) return TYPE_REG;
  return 0;
}

export class Directory {
  #entries = null; /* Array<{name, type}> once opened, null otherwise */
  #index = 0; /* next position */
  #opened = false; /* whether open()/adopt()/path ctor was given */
  #flags = BOTH;
  #mask = TYPE_MASK;
  #recursive = false;
  #closed = false;

  constructor(pathOrFd, flagsOrOpts, mask) {
    if(pathOrFd === null || pathOrFd === undefined) {
      /* empty iteration */
    } else if(typeof pathOrFd === 'number') {
      this.adopt(pathOrFd);

      if(typeof flagsOrOpts === 'number') {
        this.#flags = flagsOrOpts;

        if(typeof mask === 'number') this.#mask = mask;
      } else if(flagsOrOpts && typeof flagsOrOpts === 'object') this.#applyOpts(flagsOrOpts);
    } else if(typeof pathOrFd === 'object') {
      this.#applyOpts(pathOrFd);

      if('fd' in pathOrFd) this.adopt(pathOrFd.fd);
      else if('path' in pathOrFd) this.open(pathOrFd.path);
      else throw new Error('options object must have .fd (Number) or .path (String) property');
    } else {
      /* string path */
      this.open(String(pathOrFd));

      if(typeof flagsOrOpts === 'number') {
        this.#flags = flagsOrOpts;
        if(typeof mask === 'number') this.#mask = mask;
      } else if(flagsOrOpts && typeof flagsOrOpts === 'object') this.#applyOpts(flagsOrOpts);
    }
  }

  #applyOpts(opts) {
    if(opts.flags !== undefined) this.#flags = opts.flags | 0;
    if(opts.mask !== undefined) this.#mask = opts.mask | 0;
    if(opts.recursive !== undefined) this.#recursive = !!opts.recursive;
  }

  #load(path) {
    let names;

    try {
      names = readdirSync(path, { withFileTypes: true });
    } catch(err) {
      throw new Error(`open failed: ${err.message}`);
    }

    this.#entries = names.map(d => ({ name: d.name, type: direntType(d) }));
    this.#index = 0;
    this.#opened = true;
    this.#closed = false;
  }

  #collectRecursive(path) {
    let dirents;

    try {
      dirents = readdirSync(path, { withFileTypes: true });
    } catch(err) {
      throw new Error(`open failed: ${err.message}`);
    }

    const out = [];

    for(const d of dirents) {
      const type = direntType(d);
      out.push({ name: d.name, type });

      if(this.#recursive && type === TYPE_DIR) {
        const sub = this.#collectRecursive(path + '/' + d.name);
        for(const e of sub) out.push(e);
      }
    }
    return out;
  }

  open(path) {
    if(this.#recursive) {
      /* placeholder for future: recursive walk of paths; keep non-recursive
       * behaviour for compatibility with the C module unless requested */
    }

    this.#load(path);
    return this;
  }

  adopt(fd) {
    /* Best effort: resolve fd -> path via /proc/self/fd on Linux, then readdir. */
    let target = null;

    try {
      target = readlinkSync(`/proc/self/fd/${fd}`);
    } catch(_) {
      target = null;
    }

    if(target === null) throw new Error(`adopt(${fd}) failed: cannot resolve descriptor`);

    /* Preserve path form for recursive re-reads. */
    this.#load(target);
    return this;
  }

  close() {
    this.#entries = null;
    this.#closed = true;
    this.#opened = false;
    return this;
  }

  valueOf() {
    return this.#opened ? 1 : -1;
  }

  #decode(entry) {
    const flags = this.#flags;
    const wantName = (flags & NAME) !== 0;
    const wantType = (flags & TYPE) !== 0;
    const asBuffer = (flags & FLAG_BUFFER) !== 0;

    if(wantName && wantType) {
      const name = asBuffer ? Buffer.from(entry.name) : entry.name;
      return [name, entry.type];
    }

    if(wantName) return asBuffer ? Buffer.from(entry.name) : entry.name;

    if(wantType) return entry.type;

    return undefined;
  }

  next() {
    const mask = this.#mask;
    const flags = this.#flags;

    /* Never opened: done at once, no error */
    if(!this.#opened) return { value: undefined, done: true };

    if(!this.#entries) return { value: undefined, done: true };

    while(this.#index < this.#entries.length) {
      const entry = this.#entries[this.#index++];
      if((entry.type & mask) === 0) continue;
      return { value: this.#decode(entry), done: false };
    }

    /* Exhausted */
    const wasEmpty = this.#entries.length === 0;
    this.#entries = null;
    this.#opened = false;

    /* mirrors C: opened-but-empty directory throws */
    if(wasEmpty) throw new Error('empty directory');

    return { value: undefined, done: true };
  }

  return(value) {
    this.close();
    return { value, done: true };
  }

  throw(exception) {
    throw exception;
  }

  [Symbol.iterator]() {
    return this;
  }
}

/* Install static constants on the class and its prototype */
for(const [k, v] of Object.entries(STATICS)) {
  Object.defineProperty(Directory, k, {
    value: v,
    enumerable: true,
    writable: false,
    configurable: false,
  });

  Object.defineProperty(Directory.prototype, k, {
    value: v,
    enumerable: true,
    writable: false,
    configurable: true,
  });
}

Object.defineProperty(Directory.prototype, Symbol.toStringTag, {
  value: 'Directory',
  configurable: true,
});

export default Directory;

for(const [k, v] of Object.entries(STATICS)) Directory[k] = v;
