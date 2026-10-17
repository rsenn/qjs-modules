/* vfs.js: node:vfs-style virtual file systems (Node 26.4+, `node:vfs`).
 * exports: create, VirtualFileSystem, VirtualProvider, MemoryProvider,
 *   ComposableProvider, RealFSProvider, ArchiveProvider (default export too).
 * rule: a provider implements a few path primitives; VirtualFileSystem
 *   derives the whole fs surface (sync, callback, promises, fds) from them.
 *
 * ```js
 * import vfs from 'node:vfs';
 * const v = vfs.create();                  // MemoryProvider
 * v.mkdirSync('/dir', { recursive: true });
 * v.writeFileSync('/dir/a.txt', 'hi');
 * v.readFileSync('/dir/a.txt', 'utf8');    // 'hi'
 * await v.promises.readdir('/dir');        // ['a.txt']
 * ```
 *
 * not implemented: mount()/unmount() (no fs/import redirection),
 * streams, watch, opendir, openAsBlob, registerProvider.
 */
import * as fs from 'fs';
import { SyscallError } from 'syscallerror';
import { TextDecoder, TextEncoder } from 'textcode';
import { Directory } from 'directory';

const { Stats, Dirent, constants } = fs;
const S_IFMT = 0o170000, S_IFDIR = 0o040000, S_IFREG = 0o100000, S_IFLNK = 0o120000;
const MAX_LINKS = 40;
const ERRNO = { ELOOP: 40 }; // not exported by syscallerror

/* an errno failure: `fail('ENOENT', 'open', '/x')`, with `.path` set. */
function fail(code, syscall, path) {
  const e = new SyscallError(syscall, SyscallError[code] ?? ERRNO[code]);
  if(path !== undefined) Object.defineProperty(e, 'path', { value: path, enumerable: true, configurable: true });
  return e;
}

/* '/a/./b/../c//' -> '/a/c'; always absolute, '..' stops at the root. */
function normalize(p) {
  if(typeof p != 'string') throw new TypeError(`path must be a string, got ${typeof p}`);
  const out = [];
  for(const s of p.split('/')) {
    if(s == '' || s == '.') continue;
    if(s == '..') out.pop();
    else out.push(s);
  }
  return '/' + out.join('/');
}

const split = p => p.split('/').filter(Boolean);
const dirOf = p => p.slice(0, p.lastIndexOf('/')) || '/';
const baseOf = p => p.slice(p.lastIndexOf('/') + 1);
const child = (dir, name) => (dir == '/' ? '/' + name : dir + '/' + name);
const direntType = mode => ((mode & S_IFMT) == S_IFDIR ? Directory.TYPE_DIR : (mode & S_IFMT) == S_IFLNK ? Directory.TYPE_LNK : Directory.TYPE_REG);

function toBytes(data) {
  if(typeof data == 'string') return new TextEncoder().encode(data);
  if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if(data instanceof ArrayBuffer) return new Uint8Array(data);
  throw new TypeError('data must be a string, ArrayBuffer or TypedArray');
}

const encodingOf = o => (typeof o == 'string' ? o : o?.encoding);
const decode = (bytes, encoding) => (encoding && encoding != 'buffer' ? new TextDecoder(encoding == 'utf8' ? 'utf-8' : encoding).decode(bytes) : bytes);

function concat(a, b) {
  const r = new Uint8Array(a.length + b.length);
  r.set(a);
  r.set(b, a.length);
  return r;
}

function notImplemented(name) {
  const e = new Error(`${name}() is not implemented by this provider`);
  e.code = 'ERR_METHOD_NOT_IMPLEMENTED';
  return e;
}

/* VirtualProvider: base class of all providers (node:vfs VirtualProvider).
 *
 * Primitives a subclass implements; `path` is always absolute, normalized
 * and POSIX ('/a/b'); errors are SyscallError (ENOENT, ...):
 *
 *   statSync(path)                  fs.Stats (following symlinks)
 *   readdirSync(path)               array of entry names
 *   readFileSync(path)              Uint8Array with the whole content
 *   writeFileSync(path, bytes)      create or replace a file
 *   mkdirSync(path, recursive)      create a directory
 *   rmdirSync(path), unlinkSync(path), renameSync(from, to)
 *
 * Optional: lstatSync, readlinkSync, symlinkSync (set supportsSymlinks),
 * chmodSync, utimesSync, readonly. The rest is derived from these.
 */
export class VirtualProvider {
  get readonly() {
    return false;
  }
  get supportsSymlinks() {
    return false;
  }

  /* a missing write primitive: EROFS on a readonly provider. */
  #denied(syscall, path, name) {
    return this.readonly ? fail('EROFS', syscall, path) : notImplemented(name);
  }

  statSync(path) {
    throw notImplemented('statSync');
  }
  lstatSync(path) {
    return this.statSync(path);
  }
  readdirSync(path) {
    throw notImplemented('readdirSync');
  }
  readFileSync(path) {
    throw notImplemented('readFileSync');
  }
  writeFileSync(path, bytes, mode) {
    throw this.#denied('open', path, 'writeFileSync');
  }
  mkdirSync(path, recursive) {
    throw this.#denied('mkdir', path, 'mkdirSync');
  }
  rmdirSync(path) {
    throw this.#denied('rmdir', path, 'rmdirSync');
  }
  unlinkSync(path) {
    throw this.#denied('unlink', path, 'unlinkSync');
  }
  renameSync(from, to) {
    throw this.#denied('rename', from, 'renameSync');
  }
  readlinkSync(path) {
    throw fail('EINVAL', 'readlink', path);
  }
  symlinkSync(target, path) {
    throw this.readonly ? fail('EROFS', 'symlink', path) : fail('ENOSYS', 'symlink', path);
  }
  chmodSync(path, mode) {
    throw this.readonly ? fail('EROFS', 'chmod', path) : fail('ENOSYS', 'chmod', path);
  }
  utimesSync(path, atime, mtime) {
    throw this.readonly ? fail('EROFS', 'utimes', path) : fail('ENOSYS', 'utimes', path);
  }

  existsSync(path) {
    try {
      this.statSync(path);
      return true;
    } catch(e) {
      return false;
    }
  }

  accessSync(path, mode = 0) {
    this.statSync(path);
    if(mode & constants.W_OK && this.readonly) throw fail('EROFS', 'access', path);
  }

  appendFileSync(path, bytes) {
    let old;
    try {
      old = this.readFileSync(path);
    } catch(e) {
      if(e.code != 'ENOENT') throw e;
      old = new Uint8Array(0);
    }
    this.writeFileSync(path, concat(old, bytes));
  }

  copyFileSync(src, dest, mode = 0) {
    if(mode & constants.COPYFILE_EXCL && this.existsSync(dest)) throw fail('EEXIST', 'copyfile', dest);
    this.writeFileSync(dest, this.readFileSync(src));
  }

  truncateSync(path, len = 0) {
    const r = new Uint8Array(len);
    r.set(this.readFileSync(path).subarray(0, len));
    this.writeFileSync(path, r);
  }

  /* resolves symlinks component by component (only when supportsSymlinks). */
  realpathSync(path) {
    if(!this.supportsSymlinks) {
      this.statSync(path);
      return path;
    }
    let todo = split(path),
      cur = '/',
      hops = 0;
    while(todo.length) {
      const next = child(cur, todo.shift());
      if(this.lstatSync(next).isSymbolicLink()) {
        if(++hops > MAX_LINKS) throw fail('ELOOP', 'realpath', path);
        const t = this.readlinkSync(next);
        cur = t.startsWith('/') ? '/' : cur;
        todo = [...split(t), ...todo];
      } else cur = next;
    }
    this.statSync(cur);
    return normalize(cur);
  }

  mkdtempSync(prefix) {
    for(let i = 0; i < 100; i++) {
      const p = prefix + Math.random().toString(36).slice(2, 8).padEnd(6, '0');
      if(!this.existsSync(p)) {
        this.mkdirSync(p, false);
        return p;
      }
    }
    throw fail('EEXIST', 'mkdtemp', prefix);
  }

  rmSync(path, { recursive = false, force = false } = {}) {
    let st;
    try {
      st = this.lstatSync(path);
    } catch(e) {
      if(force && e.code == 'ENOENT') return;
      throw e;
    }
    if(!st.isDirectory()) return this.unlinkSync(path);
    if(!recursive) throw fail('EISDIR', 'rm', path);
    for(const n of this.readdirSync(path)) this.rmSync(child(path, n), { recursive, force });
    this.rmdirSync(path);
  }
}

/* inode of a MemoryProvider: dir (children), file (data) or link (target). */
function memNode(type, mode, ino) {
  const t = Date.now();
  return { type, mode, ino, atimeMs: t, mtimeMs: t, ctimeMs: t, children: type == 'dir' ? new Map() : null, data: type == 'file' ? new Uint8Array(0) : null, target: null };
}

/* MemoryProvider: files, directories and symlinks held in a Map tree.
 *
 * ```js
 * const p = new MemoryProvider();
 * p.setReadOnly();   // later writes throw EROFS; there is no way back
 * ```
 */
export class MemoryProvider extends VirtualProvider {
  #root = memNode('dir', 0o777, 1);
  #ino = 1;
  #ro = false;

  get readonly() {
    return this.#ro;
  }
  get supportsSymlinks() {
    return true;
  }
  setReadOnly() {
    this.#ro = true;
  }

  #writable(syscall, path) {
    if(this.#ro) throw fail('EROFS', syscall, path);
  }

  #lookup(path, follow, syscall, hops = { n: 0 }) {
    const parts = split(path);
    let node = this.#root;
    for(let i = 0; i < parts.length; i++) {
      if(node.type != 'dir') throw fail('ENOTDIR', syscall, path);
      const next = node.children.get(parts[i]);
      if(!next) throw fail('ENOENT', syscall, path);
      if(next.type == 'link' && (follow || i < parts.length - 1)) {
        if(++hops.n > MAX_LINKS) throw fail('ELOOP', syscall, path);
        const rest = parts.slice(i + 1).join('/');
        const base = '/' + parts.slice(0, i).join('/');
        return this.#lookup(normalize(base + '/' + next.target + '/' + rest), follow, syscall, hops);
      }
      node = next;
    }
    return node;
  }

  /* [dirNode, name] of the entry `path` would be, or throws. */
  #parent(path, syscall) {
    if(path == '/') throw fail('EEXIST', syscall, path);
    const dir = this.#lookup(dirOf(path), true, syscall);
    if(dir.type != 'dir') throw fail('ENOTDIR', syscall, path);
    return [dir, baseOf(path)];
  }

  #stats(n) {
    const size = n.type == 'file' ? n.data.length : n.type == 'link' ? n.target.length : 0;
    const type = n.type == 'dir' ? S_IFDIR : n.type == 'link' ? S_IFLNK : S_IFREG;
    return new Stats({ mode: type | n.mode, dev: 4085, ino: n.ino, nlink: 1, uid: 0, gid: 0, rdev: 0, size, blocks: Math.ceil(size / 512), atime: n.atimeMs, mtime: n.mtimeMs, ctime: n.ctimeMs });
  }

  statSync(path) {
    return this.#stats(this.#lookup(path, true, 'stat'));
  }
  lstatSync(path) {
    return this.#stats(this.#lookup(path, false, 'lstat'));
  }

  readdirSync(path) {
    const n = this.#lookup(path, true, 'scandir');
    if(n.type != 'dir') throw fail('ENOTDIR', 'scandir', path);
    return [...n.children.keys()];
  }

  readFileSync(path) {
    const n = this.#lookup(path, true, 'open');
    if(n.type == 'dir') throw fail('EISDIR', 'read', path);
    n.atimeMs = Date.now();
    return n.data.slice();
  }

  writeFileSync(path, bytes, mode = 0o666) {
    this.#writable('open', path);
    const [dir, name] = this.#parent(path, 'open');
    let n = dir.children.get(name);
    if(n?.type == 'link') n = this.#lookup(path, true, 'open');
    if(!n) dir.children.set(name, (n = memNode('file', mode, ++this.#ino)));
    else if(n.type == 'dir') throw fail('EISDIR', 'open', path);
    n.data = new Uint8Array(bytes);
    n.mtimeMs = n.ctimeMs = Date.now();
  }

  mkdirSync(path, recursive = false) {
    this.#writable('mkdir', path);
    if(recursive) {
      let cur = '/';
      for(const s of split(path)) {
        cur = child(cur, s);
        let n;
        try {
          n = this.#lookup(cur, true, 'mkdir');
        } catch(e) {
          if(e.code != 'ENOENT') throw e;
        }
        if(n && n.type != 'dir') throw fail('EEXIST', 'mkdir', cur);
        if(!n) this.mkdirSync(cur, false);
      }
      return;
    }
    const [dir, name] = this.#parent(path, 'mkdir');
    if(dir.children.has(name)) throw fail('EEXIST', 'mkdir', path);
    dir.children.set(name, memNode('dir', 0o777, ++this.#ino));
    dir.mtimeMs = dir.ctimeMs = Date.now();
  }

  rmdirSync(path) {
    this.#writable('rmdir', path);
    if(path == '/') throw fail('EBUSY', 'rmdir', path);
    const [dir, name] = this.#parent(path, 'rmdir');
    const n = dir.children.get(name);
    if(!n) throw fail('ENOENT', 'rmdir', path);
    if(n.type != 'dir') throw fail('ENOTDIR', 'rmdir', path);
    if(n.children.size) throw fail('ENOTEMPTY', 'rmdir', path);
    dir.children.delete(name);
    dir.mtimeMs = dir.ctimeMs = Date.now();
  }

  unlinkSync(path) {
    this.#writable('unlink', path);
    const [dir, name] = this.#parent(path, 'unlink');
    const n = dir.children.get(name);
    if(!n) throw fail('ENOENT', 'unlink', path);
    if(n.type == 'dir') throw fail('EISDIR', 'unlink', path);
    dir.children.delete(name);
    dir.mtimeMs = dir.ctimeMs = Date.now();
  }

  renameSync(from, to) {
    this.#writable('rename', from);
    const [sdir, sname] = this.#parent(from, 'rename');
    const n = sdir.children.get(sname);
    if(!n) throw fail('ENOENT', 'rename', from);
    if(n.type == 'dir' && (to + '/').startsWith(from + '/')) throw fail('EINVAL', 'rename', to);
    const [ddir, dname] = this.#parent(to, 'rename');
    const old = ddir.children.get(dname);
    if(old === n) return;
    if(old) {
      if(old.type == 'dir' && n.type != 'dir') throw fail('EISDIR', 'rename', to);
      if(old.type != 'dir' && n.type == 'dir') throw fail('ENOTDIR', 'rename', to);
      if(old.type == 'dir' && old.children.size) throw fail('ENOTEMPTY', 'rename', to);
    }
    sdir.children.delete(sname);
    ddir.children.set(dname, n);
    n.ctimeMs = Date.now();
  }

  readlinkSync(path) {
    const n = this.#lookup(path, false, 'readlink');
    if(n.type != 'link') throw fail('EINVAL', 'readlink', path);
    return n.target;
  }

  symlinkSync(target, path) {
    this.#writable('symlink', path);
    const [dir, name] = this.#parent(path, 'symlink');
    if(dir.children.has(name)) throw fail('EEXIST', 'symlink', path);
    const n = memNode('link', 0o777, ++this.#ino);
    n.target = String(target);
    dir.children.set(name, n);
  }

  chmodSync(path, mode) {
    this.#writable('chmod', path);
    const n = this.#lookup(path, true, 'chmod');
    n.mode = mode & 0o7777;
    n.ctimeMs = Date.now();
  }

  /* atime/mtime: Date, or seconds as in fs.utimes. */
  utimesSync(path, atime, mtime) {
    this.#writable('utimes', path);
    const ms = t => (t instanceof Date ? t.getTime() : t * 1000);
    const n = this.#lookup(path, true, 'utimes');
    n.atimeMs = ms(atime);
    n.mtimeMs = ms(mtime);
  }
}

/* RealFSProvider: a directory of the real file system as the VFS root.
 * `/a` is `<root>/a`; '..' cannot leave the root, symlinks are not checked
 * (a path mapping, not a sandbox).
 *
 * ```js
 * vfs.create(new RealFSProvider('/tmp/vfs-root'));
 * ```
 */
export class RealFSProvider extends VirtualProvider {
  #root;

  constructor(rootPath) {
    super();
    if(typeof rootPath != 'string' || !rootPath) throw new TypeError('rootPath must be a non-empty string');
    this.#root = rootPath.replace(/\/+$/, '');
  }

  get rootPath() {
    return this.#root || '/';
  }
  get supportsSymlinks() {
    return true;
  }

  #real(path) {
    return path == '/' ? this.rootPath : this.#root + path;
  }

  statSync(path) {
    return fs.statSync(this.#real(path));
  }
  lstatSync(path) {
    return fs.lstatSync(this.#real(path));
  }
  readdirSync(path) {
    return fs.readdirSync(this.#real(path));
  }
  readFileSync(path) {
    return new Uint8Array(fs.readFileSync(this.#real(path)));
  }
  writeFileSync(path, bytes, mode = 0o666) {
    fs.writeFileSync(this.#real(path), bytes, { mode, overwrite: true });
  }
  mkdirSync(path, recursive) {
    fs.mkdirSync(this.#real(path), { recursive });
  }
  rmdirSync(path) {
    fs.rmdirSync(this.#real(path));
  }
  unlinkSync(path) {
    fs.unlinkSync(this.#real(path));
  }
  renameSync(from, to) {
    fs.renameSync(this.#real(from), this.#real(to));
  }
  readlinkSync(path) {
    return fs.readlinkSync(this.#real(path));
  }
  symlinkSync(target, path) {
    fs.symlinkSync(target, this.#real(path));
  }
  chmodSync(path, mode) {
    fs.chmodSync(this.#real(path), mode);
  }
  utimesSync(path, atime, mtime) {
    fs.utimesSync(this.#real(path), atime, mtime);
  }
}

/* ComposableProvider: layers providers; the first is the writable layer.
 *
 *   reads      first layer that has the path wins
 *   readdir    names of all layers merged
 *   writes     go to the first layer (parents are created there)
 *   remove     hides the path from lower layers, they stay untouched
 *
 * ```js
 * new ComposableProvider([new MemoryProvider(), new RealFSProvider(dir)]);
 * ```
 */
export class ComposableProvider extends VirtualProvider {
  #layers;
  #hidden = new Set();

  constructor(providers) {
    super();
    if(!Array.isArray(providers) || !providers.length) throw new TypeError('providers must be a non-empty array');
    this.#layers = [...providers];
  }

  get providers() {
    return [...this.#layers];
  }
  get readonly() {
    return this.#layers[0].readonly;
  }
  get supportsSymlinks() {
    return this.#layers[0].supportsSymlinks;
  }

  #isHidden(path) {
    for(let p = path; ; p = dirOf(p)) {
      if(this.#hidden.has(p)) return true;
      if(p == '/') return false;
    }
  }

  /* first layer holding `path`, or undefined. */
  #owner(path) {
    if(this.#isHidden(path)) return;
    return this.#layers.find(l => this.#lexists(l, path));
  }

  #lexists(l, path) {
    try {
      l.lstatSync(path);
      return true;
    } catch(e) {
      return false;
    }
  }

  #need(path, syscall) {
    const l = this.#owner(path);
    if(!l) throw fail('ENOENT', syscall, path);
    return l;
  }

  /* creates the missing parents of `path` in the first layer. */
  #parents(path, syscall) {
    const dir = dirOf(path);
    if(this.#need(dir, syscall).statSync(dir).isDirectory()) this.#layers[0].mkdirSync(dir, true);
    else throw fail('ENOTDIR', syscall, path);
  }

  statSync(path) {
    return this.#need(path, 'stat').statSync(path);
  }
  lstatSync(path) {
    return this.#need(path, 'lstat').lstatSync(path);
  }
  readFileSync(path) {
    return this.#need(path, 'open').readFileSync(path);
  }
  readlinkSync(path) {
    return this.#need(path, 'readlink').readlinkSync(path);
  }

  readdirSync(path) {
    this.#need(path, 'scandir');
    const names = new Set();
    for(const l of this.#layers) {
      let st;
      try {
        st = l.statSync(path);
      } catch(e) {
        continue;
      }
      if(!st.isDirectory()) break;
      for(const n of l.readdirSync(path)) if(!this.#isHidden(child(path, n))) names.add(n);
    }
    return [...names];
  }

  writeFileSync(path, bytes, mode) {
    this.#parents(path, 'open');
    this.#layers[0].writeFileSync(path, bytes, mode);
    this.#hidden.delete(path);
  }

  mkdirSync(path, recursive = false) {
    if(this.#owner(path)) {
      if(recursive && this.statSync(path).isDirectory()) return;
      throw fail('EEXIST', 'mkdir', path);
    }
    if(recursive) this.#parents(path, 'mkdir');
    this.#layers[0].mkdirSync(path, false);
    this.#hidden.delete(path);
  }

  symlinkSync(target, path) {
    if(this.#owner(path)) throw fail('EEXIST', 'symlink', path);
    this.#parents(path, 'symlink');
    this.#layers[0].symlinkSync(target, path);
  }

  #remove(path, syscall, op) {
    const owner = this.#need(path, syscall);
    if(owner === this.#layers[0]) this.#layers[0][op](path);
    if(this.#layers.slice(owner === this.#layers[0] ? 1 : 0).some(l => this.#lexists(l, path))) this.#hidden.add(path);
  }

  rmdirSync(path) {
    if(!this.statSync(path).isDirectory()) throw fail('ENOTDIR', 'rmdir', path);
    if(this.readdirSync(path).length) throw fail('ENOTEMPTY', 'rmdir', path);
    this.#remove(path, 'rmdir', 'rmdirSync');
  }

  unlinkSync(path) {
    if(this.lstatSync(path).isDirectory()) throw fail('EISDIR', 'unlink', path);
    this.#remove(path, 'unlink', 'unlinkSync');
  }

  /* copy-up rename: copies the tree into the first layer, removes the source. */
  renameSync(from, to) {
    const st = this.lstatSync(from);
    if(st.isDirectory() && (to + '/').startsWith(from + '/')) throw fail('EINVAL', 'rename', to);
    if(this.#owner(to)) this.rmSync(to, { recursive: true });
    this.#parents(to, 'rename');
    const copy = (a, b) => {
      const s = this.lstatSync(a);
      if(s.isDirectory()) {
        this.mkdirSync(b);
        for(const n of this.readdirSync(a)) copy(child(a, n), child(b, n));
      } else if(s.isSymbolicLink()) this.symlinkSync(this.readlinkSync(a), b);
      else this.writeFileSync(b, this.readFileSync(a), s.mode & 0o7777);
    };
    copy(from, to);
    this.rmSync(from, { recursive: true });
  }

  /* copy-up: a lower layer's file is copied to the first layer first. */
  #up(path, syscall) {
    const l = this.#need(path, syscall);
    if(l !== this.#layers[0]) {
      this.#parents(path, syscall);
      this.#layers[0].writeFileSync(path, l.readFileSync(path), l.statSync(path).mode & 0o7777);
    }
  }

  chmodSync(path, mode) {
    this.#up(path, 'chmod');
    this.#layers[0].chmodSync(path, mode);
  }
  utimesSync(path, atime, mtime) {
    this.#up(path, 'utimes');
    this.#layers[0].utimesSync(path, atime, mtime);
  }
}

/* archive is a soft dependency: loaded on first use (requireModule), so a
 * build without the native `archive` (libarchive) still has the rest. */
let archiveModule;
const archiveApi = () => (archiveModule ??= globalThis.requireModule('archive'));

/* ArchiveProvider: a tar/zip/... file (libarchive) as a read-only VFS.
 * The entry list is indexed on first use; file content is read by
 * re-scanning the archive, so reads cost one pass over the archive.
 *
 * ```js
 * vfs.create(new ArchiveProvider('/path/data.tar.gz'));
 * ```
 */
export class ArchiveProvider extends VirtualProvider {
  #file;
  #nodes = null;

  constructor(file) {
    super();
    if(typeof file != 'string' || !file) throw new TypeError('file must be a non-empty string');
    this.#file = file;
  }

  get readonly() {
    return true;
  }
  get supportsSymlinks() {
    return true;
  }

  #index() {
    if(this.#nodes) return this.#nodes;
    const newDir = () => ({ type: 'dir', mode: 0o755, size: 0, mtimeMs: 0, names: new Set() });
    const nodes = new Map([['/', newDir()]]);
    const ensure = path => {
      let n = nodes.get(path);
      if(!n) {
        nodes.set(path, (n = newDir()));
        ensure(dirOf(path)).names.add(baseOf(path));
      }
      return n;
    };
    const ar = new (archiveApi().Archive)().open(this.#file);
    try {
      for(const e of ar) {
        const path = normalize('/' + e.pathname);
        if(path == '/') continue;
        const type = e.type == 'directory' ? 'dir' : e.symlink ? 'link' : 'file';
        const prev = nodes.get(path);
        const n = { type, mode: e.mode & 0o7777, size: type == 'file' ? e.size : 0, mtimeMs: e.mtime?.getTime?.() ?? 0, uid: e.uid, gid: e.gid, target: e.symlink, names: type == 'dir' ? (prev?.names ?? new Set()) : null, pathname: e.pathname };
        nodes.set(path, n);
        ensure(dirOf(path)).names.add(baseOf(path));
      }
    } finally {
      ar.close();
    }
    return (this.#nodes = nodes);
  }

  #get(path, follow, syscall, hops = 0) {
    const nodes = this.#index();
    let cur = '/',
      n = nodes.get('/');
    const parts = split(path);
    for(let i = 0; i < parts.length; i++) {
      if(n.type != 'dir') throw fail('ENOTDIR', syscall, path);
      cur = child(cur, parts[i]);
      n = nodes.get(cur);
      if(!n) throw fail('ENOENT', syscall, path);
      if(n.type == 'link' && (follow || i < parts.length - 1)) {
        if(hops >= MAX_LINKS) throw fail('ELOOP', syscall, path);
        return this.#get(normalize(dirOf(cur) + '/' + n.target + '/' + parts.slice(i + 1).join('/')), follow, syscall, hops + 1);
      }
    }
    return n;
  }

  #stats(n) {
    const type = n.type == 'dir' ? S_IFDIR : n.type == 'link' ? S_IFLNK : S_IFREG;
    return new Stats({ mode: type | n.mode, dev: 4085, ino: 0, nlink: 1, uid: n.uid ?? 0, gid: n.gid ?? 0, rdev: 0, size: n.type == 'link' ? n.target.length : n.size, blocks: Math.ceil(n.size / 512), atime: n.mtimeMs, mtime: n.mtimeMs, ctime: n.mtimeMs });
  }

  statSync(path) {
    return this.#stats(this.#get(path, true, 'stat'));
  }
  lstatSync(path) {
    return this.#stats(this.#get(path, false, 'lstat'));
  }

  readdirSync(path) {
    const n = this.#get(path, true, 'scandir');
    if(n.type != 'dir') throw fail('ENOTDIR', 'scandir', path);
    return [...n.names];
  }

  readlinkSync(path) {
    const n = this.#get(path, false, 'readlink');
    if(n.type != 'link') throw fail('EINVAL', 'readlink', path);
    return n.target;
  }

  readFileSync(path) {
    const n = this.#get(path, true, 'open');
    if(n.type == 'dir') throw fail('EISDIR', 'read', path);
    const out = new Uint8Array(n.size);
    const ar = new (archiveApi().Archive)().open(this.#file);
    try {
      for(let e; (e = ar.next()); ) {
        if(e.pathname != n.pathname) continue;
        for(let off = 0, r; off < n.size && (r = ar.read(out.buffer, off, n.size - off)) > 0; ) off += r;
        break;
      }
    } finally {
      ar.close();
    }
    return out;
  }
}

/* fd open flags: 'r' 'r+' 'w' 'wx' 'w+' 'a' 'a+' ... or O_* numbers. */
function parseFlags(flags) {
  if(typeof flags == 'number') {
    const acc = flags & 3;
    return { read: acc != 1, write: acc != 0, create: !!(flags & constants.O_CREAT), excl: !!(flags & constants.O_EXCL), trunc: !!(flags & constants.O_TRUNC), append: !!(flags & constants.O_APPEND) };
  }
  const m = /^([rwa])([+xs]*)$/.exec(flags);
  if(!m) throw new TypeError(`invalid flags '${flags}'`);
  const [, b, rest] = m;
  const plus = rest.includes('+');
  return { read: b == 'r' || plus, write: b != 'r' || plus, create: b != 'r', excl: rest.includes('x'), trunc: b == 'w', append: b == 'a' };
}

/* methods that also exist as callback (`name(...args, cb)`) and promise
 * (`vfs.promises.name(...args)`) forms, each derived from `<name>Sync`. */
const ASYNC = ['access', 'appendFile', 'chmod', 'close', 'copyFile', 'fstat', 'ftruncate', 'lstat', 'mkdir', 'mkdtemp', 'open', 'read', 'readFile', 'readdir', 'readlink', 'realpath', 'rename', 'rm', 'rmdir', 'stat', 'symlink', 'truncate', 'unlink', 'utimes', 'write', 'writeFile'];
const PROMISES = ASYNC.filter(n => !/^(close|fstat|ftruncate|open|read|write)$/.test(n));

/* VirtualFileSystem: the fs-like API over a provider (node:vfs).
 *
 * ```js
 * const v = new VirtualFileSystem(provider);       // MemoryProvider default
 * v.writeFileSync('/a', 'x'); v.readFileSync('/a', 'utf8');
 * v.readFile('/a', 'utf8', (err, s) => {});
 * await v.promises.readFile('/a', 'utf8');
 * ```
 *
 * Paths are absolute POSIX ('..' stops at '/'); relative ones are taken
 * from '/'. readFileSync returns a Uint8Array without an encoding.
 * File descriptors (openSync, readSync, ...) are numbers >= 10000, buffered
 * in memory and written to the provider on closeSync/fsyncSync or before
 * the next path-based call.
 *
 * throws SyscallError (`code`, `path`) like fs; TypeError for bad types.
 */
export class VirtualFileSystem {
  #provider;
  #fds = new Map();
  #nextFd = 10000;
  #promises = null;

  constructor(provider = new MemoryProvider(), options = {}) {
    if(!(provider instanceof VirtualProvider)) throw new TypeError('provider must be a VirtualProvider');
    this.#provider = provider;
  }

  get provider() {
    return this.#provider;
  }
  get readonly() {
    return this.#provider.readonly;
  }

  get promises() {
    return (this.#promises ??= Object.fromEntries(PROMISES.map(n => [n, async (...a) => this[n + 'Sync'](...a)])));
  }

  /* the provider, with every open fd's pending writes flushed to it. */
  #p() {
    for(const f of this.#fds.values()) this.#flush(f);
    return this.#provider;
  }

  #flush(f) {
    if(!f.dirty) return;
    this.#provider.writeFileSync(f.path, f.buf.subarray(0, f.len));
    f.dirty = false;
  }

  #fd(fd, syscall) {
    const f = this.#fds.get(fd);
    if(!f) throw fail('EBADF', syscall);
    return f;
  }

  existsSync(path) {
    return this.#p().existsSync(normalize(path));
  }

  statSync(path, options) {
    try {
      return this.#p().statSync(normalize(path));
    } catch(e) {
      if(options?.throwIfNoEntry === false && e.code == 'ENOENT') return undefined;
      throw e;
    }
  }

  lstatSync(path, options) {
    try {
      return this.#p().lstatSync(normalize(path));
    } catch(e) {
      if(options?.throwIfNoEntry === false && e.code == 'ENOENT') return undefined;
      throw e;
    }
  }

  readFileSync(path, options) {
    return decode(this.#p().readFileSync(normalize(path)), encodingOf(options));
  }

  writeFileSync(path, data, options) {
    const p = normalize(path),
      bytes = toBytes(data),
      flag = options?.flag ?? 'w';
    if(flag[0] == 'a') this.#p().appendFileSync(p, bytes);
    else {
      if(flag.includes('x') && this.existsSync(p)) throw fail('EEXIST', 'open', p);
      this.#p().writeFileSync(p, bytes, options?.mode);
    }
  }

  appendFileSync(path, data, options) {
    this.#p().appendFileSync(normalize(path), toBytes(data));
  }

  readdirSync(path, options) {
    const p = normalize(path),
      prov = this.#p(),
      files = typeof options == 'object' && options?.withFileTypes,
      out = [];
    const walk = (dir, rel) => {
      for(const name of prov.readdirSync(dir)) {
        const full = child(dir, name),
          r = rel ? rel + '/' + name : name;
        out.push(files ? Object.assign(new Dirent(name, direntType(prov.lstatSync(full).mode)), { parentPath: dir, path: dir }) : r);
        if(options?.recursive && prov.lstatSync(full).isDirectory()) walk(full, r);
      }
    };
    walk(p, '');
    return out;
  }

  /* mkdirSync(path, { recursive, mode }); returns the first directory
   * created when recursive, as node does. */
  mkdirSync(path, options) {
    const recursive = typeof options == 'object' && !!options?.recursive,
      prov = this.#p();
    let p = normalize(path),
      first;
    if(recursive) {
      for(let d = p; d != '/' && !prov.existsSync(d); d = dirOf(d)) first = d;
    }
    prov.mkdirSync(p, recursive);
    return first;
  }

  rmdirSync(path) {
    this.#p().rmdirSync(normalize(path));
  }
  unlinkSync(path) {
    this.#p().unlinkSync(normalize(path));
  }
  renameSync(from, to) {
    this.#p().renameSync(normalize(from), normalize(to));
  }
  copyFileSync(src, dest, mode) {
    this.#p().copyFileSync(normalize(src), normalize(dest), mode);
  }
  realpathSync(path) {
    return this.#p().realpathSync(normalize(path));
  }
  readlinkSync(path) {
    return this.#p().readlinkSync(normalize(path));
  }
  symlinkSync(target, path) {
    this.#p().symlinkSync(target, normalize(path));
  }
  accessSync(path, mode) {
    this.#p().accessSync(normalize(path), mode);
  }
  rmSync(path, options) {
    this.#p().rmSync(normalize(path), options);
  }
  truncateSync(path, len) {
    this.#p().truncateSync(normalize(path), len);
  }
  chmodSync(path, mode) {
    this.#p().chmodSync(normalize(path), mode);
  }
  utimesSync(path, atime, mtime) {
    this.#p().utimesSync(normalize(path), atime, mtime);
  }
  mkdtempSync(prefix) {
    return this.#p().mkdtempSync(normalize(prefix));
  }

  openSync(path, flags = 'r', mode = 0o666) {
    const f = parseFlags(flags),
      p = normalize(path),
      prov = this.#p();
    let st;
    try {
      st = prov.statSync(p);
    } catch(e) {
      if(e.code != 'ENOENT') throw e;
    }
    let data = new Uint8Array(0),
      dirty = false;
    if(!st) {
      if(!f.create) throw fail('ENOENT', 'open', p);
      prov.writeFileSync(p, data, mode);
    } else {
      if(f.create && f.excl) throw fail('EEXIST', 'open', p);
      if(st.isDirectory()) throw fail('EISDIR', 'open', p);
      if(f.write && prov.readonly) throw fail('EROFS', 'open', p);
      if(f.trunc && f.write) dirty = true;
      else data = new Uint8Array(prov.readFileSync(p));
    }
    const fd = this.#nextFd++;
    this.#fds.set(fd, { path: p, buf: data, len: data.length, pos: 0, dirty, ...f });
    return fd;
  }

  closeSync(fd) {
    const f = this.#fd(fd, 'close');
    this.#flush(f);
    this.#fds.delete(fd);
  }

  fsyncSync(fd) {
    this.#flush(this.#fd(fd, 'fsync'));
  }

  fstatSync(fd) {
    const f = this.#fd(fd, 'fstat');
    this.#flush(f);
    return this.#provider.statSync(f.path);
  }

  /* readSync(fd, buffer, offset = 0, length = rest, position = null)
   * returns the bytes read; position null continues from the fd position. */
  readSync(fd, buffer, offset = 0, length = buffer.byteLength - offset, position = null) {
    const f = this.#fd(fd, 'read');
    if(!f.read) throw fail('EBADF', 'read');
    const at = position ?? f.pos;
    const n = Math.max(0, Math.min(length, f.len - at));
    new Uint8Array(buffer.buffer ?? buffer, (buffer.byteOffset ?? 0) + offset, n).set(f.buf.subarray(at, at + n));
    if(position === null) f.pos += n;
    return n;
  }

  /* writeSync(fd, buffer, offset = 0, length = rest, position = null) or
   * writeSync(fd, string, position = null); returns the bytes written. */
  writeSync(fd, data, a, b, c) {
    const f = this.#fd(fd, 'write');
    if(!f.write) throw fail('EBADF', 'write');
    let bytes, position;
    if(typeof data == 'string') {
      bytes = toBytes(data);
      position = a ?? null;
    } else {
      const src = toBytes(data),
        offset = a ?? 0;
      bytes = src.subarray(offset, offset + (b ?? src.length - offset));
      position = c ?? null;
    }
    const at = f.append ? f.len : (position ?? f.pos);
    if(at + bytes.length > f.buf.length) {
      const nb = new Uint8Array(Math.max(at + bytes.length, f.buf.length * 2));
      nb.set(f.buf.subarray(0, f.len));
      f.buf = nb;
    }
    f.buf.set(bytes, at);
    f.len = Math.max(f.len, at + bytes.length);
    if(position === null) f.pos = at + bytes.length;
    f.dirty = true;
    return bytes.length;
  }

  ftruncateSync(fd, len = 0) {
    const f = this.#fd(fd, 'ftruncate');
    if(!f.write) throw fail('EINVAL', 'ftruncate');
    const nb = new Uint8Array(Math.max(len, 0));
    nb.set(f.buf.subarray(0, Math.min(len, f.len)));
    f.buf = nb;
    f.len = nb.length;
    f.dirty = true;
  }
}

/* callback forms: `v.readFile('/a', 'utf8', (err, data) => {})`; read and
 * write pass the buffer on, like node: cb(err, bytes, buffer). */
for(const name of ASYNC) {
  VirtualFileSystem.prototype[name] = function(...args) {
    const cb = args.pop();
    if(typeof cb != 'function') throw new TypeError('callback must be a function');
    let result, error;
    try {
      result = this[name + 'Sync'](...args);
    } catch(e) {
      error = e;
    }
    Promise.resolve().then(() => (error ? cb(error) : name == 'read' || name == 'write' ? cb(null, result, args[1]) : cb(null, result)));
  };
}

Object.defineProperty(VirtualFileSystem.prototype, Symbol.toStringTag, { value: 'VirtualFileSystem', configurable: true });

/* create: vfs.create([provider][, options]); new VirtualFileSystem(...). */
export function create(provider, options) {
  return new VirtualFileSystem(provider, options);
}

export default { create, VirtualFileSystem, VirtualProvider, MemoryProvider, ComposableProvider, RealFSProvider, ArchiveProvider };
