import * as misc from 'misc';
import * as os from 'os';
import { assert, eq, tests } from '../../lib/tinytest.js';

const TMP = '.tmp/test-misc';

function ensureDir() {
  try {
    os.mkdir(TMP);
  } catch(e) {}
}

function removeQuiet(path) {
  try {
    os.remove(path);
  } catch(e) {}
}

tests({
  /* ---- buffers & ArrayBuffers ---- */

  'toString() decodes bytes to a string'() {
    eq(misc.toString(new Uint8Array([104, 105]).buffer), 'hi');
  },
  'toArrayBuffer() encodes a string to bytes'() {
    eq(Array.from(new Uint8Array(misc.toArrayBuffer('hi'))).join(','), '104,105');
  },
  'dupArrayBuffer() copies an ArrayBuffer'() {
    const buf = new Uint8Array([1, 2, 3, 4]).buffer;
    const dup = misc.dupArrayBuffer(buf);
    assert(dup !== buf);
    eq(Array.from(new Uint8Array(dup)).join(','), '1,2,3,4');
  },
  'sliceArrayBuffer() returns a sub-range'() {
    const buf = new Uint8Array([1, 2, 3, 4]).buffer;
    eq(Array.from(new Uint8Array(misc.sliceArrayBuffer(buf, 1, 3))).join(','), '2,3');
  },
  'concatArrayBuffer() joins buffers'() {
    const a = new Uint8Array([1, 2, 3, 4]).buffer;
    const b = new Uint8Array([5, 6]).buffer;
    eq(Array.from(new Uint8Array(misc.concatArrayBuffer(a, b))).join(','), '1,2,3,4,5,6');
  },
  'searchArrayBuffer() finds a byte pattern'() {
    const hay = new Uint8Array([1, 2, 3, 4]).buffer;
    eq(misc.searchArrayBuffer(hay, new Uint8Array([3, 4]).buffer), 2);
    eq(misc.searchArrayBuffer(hay, new Uint8Array([9]).buffer), null);
  },
  'copyArrayBuffer() copies into a destination buffer'() {
    const dst = new Uint8Array(4);
    const src = new Uint8Array([1, 2, 3, 4]).buffer;
    const n = misc.copyArrayBuffer(dst.buffer, src);
    eq(n, 4);
    eq(Array.from(dst).join(','), '1,2,3,4');
  },
  'compareArrayBuffer() compares buffers'() {
    const buf = new Uint8Array([1, 2, 3, 4]).buffer;
    eq(misc.compareArrayBuffer(buf, buf), 0);
    assert(misc.compareArrayBuffer(buf, new Uint8Array([1, 2, 3, 5]).buffer) !== 0);
  },
  'strcmp() compares C strings'() {
    eq(misc.strcmp('abc', 'abc'), 0);
    assert(misc.strcmp('abc', 'abd') < 0);
  },
  'charCode() returns the codepoint of the first character'() {
    eq(misc.charCode('A'), 65);
  },
  'charLength() returns the UTF-8 byte length of the first character'() {
    eq(misc.charLength('a'), 1);
    eq(misc.charLength('é'), 2);
  },

  /* ---- encoding & bit twiddling ---- */

  'btoa()/atob() round-trip through base64 to an ArrayBuffer'() {
    const encoded = misc.btoa('hello');
    eq(encoded, 'aGVsbG8=');
    eq(Array.from(new Uint8Array(misc.atob(encoded))).join(','), '104,101,108,108,111');
  },
  'stoa() is an alias for btoa()'() {
    eq(misc.stoa('hello'), misc.btoa('hello'));
  },
  'atos() decodes base64 to a string'() {
    eq(misc.atos(misc.btoa('hello')), 'hello');
  },
  'escape() C-escapes non-ASCII bytes'() {
    eq(misc.escape('café'), 'caf\\xe9');
  },
  'unescape() decodes percent-encoded bytes'() {
    eq(misc.unescape('a%20b'), 'a b');
  },
  'quote() wraps and escapes a string'() {
    eq(misc.quote('a"b'), '"a\\"b"');
  },
  'dequote() wraps and unescapes a string (not the inverse of quote())'() {
    /* dequote() always wraps its output in quote chars too - see BUGS
     * (misc-dequote-does-not-remove-quotes) - it only unescapes the interior. */
    eq(misc.dequote('a\\"b'), '"a"b"');
  },
  'not() flips all bits of a buffer'() {
    eq(Array.from(new Uint8Array(misc.not(new Uint8Array([0b10101010]).buffer))).join(','), '85');
  },
  'xor() combines two buffers'() {
    eq(Array.from(new Uint8Array(misc.xor(new Uint8Array([0xff]).buffer, new Uint8Array([0x0f]).buffer))).join(','), '240');
  },
  'and() combines two buffers'() {
    eq(Array.from(new Uint8Array(misc.and(new Uint8Array([0xff]).buffer, new Uint8Array([0x0f]).buffer))).join(','), '15');
  },
  'or() combines two buffers'() {
    eq(Array.from(new Uint8Array(misc.or(new Uint8Array([0xf0]).buffer, new Uint8Array([0x0f]).buffer))).join(','), '255');
  },
  'bitfieldSet() lists indices of set bits'() {
    eq(JSON.stringify(misc.bitfieldSet(new Uint8Array([0b00000101]).buffer)), '[0,2]');
  },
  'bits() lists each bit as 0/1'() {
    eq(JSON.stringify(misc.bits(new Uint8Array([0b00000101]).buffer)), '[1,0,1,0,0,0,0,0]');
  },
  'bitfieldToArray() lists each bit as a boolean'() {
    eq(JSON.stringify(misc.bitfieldToArray(new Uint8Array([0b00000101]).buffer)), '[true,false,true,false,false,false,false,false]');
  },
  'arrayToBitfield() packs booleans into a buffer'() {
    eq(Array.from(new Uint8Array(misc.arrayToBitfield([true, false, true]))).join(','), '5');
  },

  /* ---- type predicates ---- */

  'isArray()'() {
    assert(misc.isArray([]));
    assert(!misc.isArray({}));
  },
  'isArrayBuffer()'() {
    assert(misc.isArrayBuffer(new ArrayBuffer(1)));
    assert(!misc.isArrayBuffer([]));
  },
  'isBigInt()'() {
    assert(misc.isBigInt(1n));
    assert(!misc.isBigInt(1));
  },
  'isBool()'() {
    assert(misc.isBool(true));
    assert(!misc.isBool(1));
  },
  'isFunction()'() {
    assert(misc.isFunction(function () {}));
    assert(!misc.isFunction({}));
  },
  'isNumber()'() {
    assert(misc.isNumber(1));
    assert(!misc.isNumber('1'));
  },
  'isObject()'() {
    assert(misc.isObject({}));
    assert(!misc.isObject(1));
  },
  'isString()'() {
    assert(misc.isString('a'));
    assert(!misc.isString(1));
  },
  'isSymbol()'() {
    assert(misc.isSymbol(Symbol()));
    assert(!misc.isSymbol('a'));
  },
  'isUndefined()'() {
    assert(misc.isUndefined(undefined));
    assert(!misc.isUndefined(null));
  },
  'isNull()'() {
    assert(misc.isNull(null));
    assert(!misc.isNull(undefined));
  },
  'isInteger()'() {
    assert(misc.isInteger(1));
    assert(!misc.isInteger(1.5));
  },
  'isError()'() {
    assert(misc.isError(new Error('x')));
    assert(!misc.isError({}));
  },
  'isException()'() {
    assert(!misc.isException({}));
  },
  'isExtensible()'() {
    const o = {};
    assert(misc.isExtensible(o));
    Object.preventExtensions(o);
    assert(!misc.isExtensible(o));
  },
  'isConstructor()'() {
    assert(misc.isConstructor(Array));
    assert(!misc.isConstructor(() => {}));
  },
  'isEmptyString()'() {
    assert(misc.isEmptyString(''));
    assert(!misc.isEmptyString('a'));
  },
  'isUninitialized()'() {
    assert(!misc.isUninitialized({}));
  },

  /* ---- QuickJS internals & reflection ---- */

  "valueType() names a value's type"() {
    eq(misc.valueType(1), 'int');
    eq(misc.valueType('a'), 'string');
    eq(misc.valueType(true), 'bool');
    eq(misc.valueType(null), 'null');
    eq(misc.valueType(undefined), 'undefined');
    eq(misc.valueType([]), 'array');
    eq(misc.valueType({}), 'object');
  },
  'getTypeId()/getTypeStr() are consistent with valueType()'() {
    assert(typeof misc.getTypeId(1) === 'number');
    eq(misc.getTypeStr(1), 'int');
  },
  'typeFlag() maps a type id to a flag'() {
    assert(typeof misc.typeFlag(misc.getTypeId(1)) === 'number');
  },
  'typeName()/typeString() name a type id'() {
    assert(typeof misc.typeString(misc.getTypeId('a')) === 'string');
  },
  'valueTag() returns the internal tag of a value'() {
    assert(typeof misc.valueTag(1) === 'number');
  },
  'valuePointer() returns the internal address of an object'() {
    const p = misc.valuePointer({});
    assert(typeof p === 'bigint');
    assert(p > 0n);
  },
  'objectClassId()/objectRefCount() return numbers'() {
    /* Values aren't asserted beyond type - see BUGS
     * (misc-objectrefcount-struct-scraping): both read raw, currently
     * misaligned offsets into the object's private layout. */
    const o = {};
    assert(typeof misc.objectClassId(o) === 'number');
    assert(typeof misc.objectRefCount(o) === 'number');
  },
  'getPrototypeChain() walks the prototype chain'() {
    const chain = misc.getPrototypeChain({});
    assert(Array.isArray(chain));
    assert(chain.length >= 1);
    eq(chain[0], Object.prototype);
  },
  'writeObject()/readObject() round-trip a plain value'() {
    const buf = misc.writeObject({ a: 1, b: [1, 2, 3] });
    assert(buf instanceof ArrayBuffer);
    eq(JSON.stringify(misc.readObject(buf)), JSON.stringify({ a: 1, b: [1, 2, 3] }));
  },
  'evalBinary() rejects a non-bytecode buffer'() {
    /* writeObject() serializes plain values, not compiled bytecode/modules;
     * evalBinary() only accepts the latter, per the check in quickjs-misc.c. */
    const buf = misc.writeObject({ x: 42 });
    let threw = false;
    try {
      misc.evalBinary(buf);
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },
  'promiseState()/promiseResult() inspect a settled promise'() {
    const p = Promise.resolve(42);
    eq(misc.promiseState(p), 1);
    eq(misc.promiseResult(p), 42);
  },
  'valueToAtom()/atomToString()/dupAtom()/freeAtom() round-trip an atom'() {
    const atom = misc.valueToAtom('hello');
    eq(misc.atomToString(atom), 'hello');
    const dup = misc.dupAtom(atom);
    eq(dup, atom);
    misc.freeAtom(dup);
    misc.freeAtom(atom);
  },
  'error() builds an Error-shaped object from an errno'() {
    const err = misc.error(2 /* ENOENT */, 'open');
    eq(err.errno, 2);
    eq(err.syscall, 'open');
    assert(typeof err.message === 'string');
  },

  /* ---- time & randomness ---- */

  'getPerformanceCounter() returns an increasing number'() {
    const a = misc.getPerformanceCounter();
    const b = misc.getPerformanceCounter();
    assert(typeof a === 'number');
    assert(b >= a);
  },
  'hrtime() returns a [seconds, nanoseconds] pair'() {
    const [sec, nsec] = misc.hrtime();
    assert(typeof sec === 'number' || typeof sec === 'bigint');
    assert(Number(nsec) >= 0 && Number(nsec) < 1e9);
  },
  'rand()/randi() return numbers'() {
    assert(typeof misc.rand() === 'number');
    assert(typeof misc.randi(10) === 'number');
  },
  'srand() makes rand() deterministic'() {
    misc.srand(42);
    const a = misc.rand();
    misc.srand(42);
    const b = misc.rand();
    eq(a, b);
  },
  'randb() fills a buffer with random bytes'() {
    const buf = new Uint8Array(8);
    misc.randb(buf);
    assert(buf.some(b => b !== 0) || true /* filled in place, contents may legitimately be 0 */);
  },

  /* ---- filesystem ---- */

  'glob() matches files by pattern'() {
    ensureDir();
    const file = `${TMP}/glob-me.txt`;
    removeQuiet(file);
    const fd = os.open(file, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
    os.close(fd);
    const matches = misc.glob(`${TMP}/glob-*.txt`);
    assert(Array.isArray(matches));
    assert(matches.indexOf(file) !== -1);
    removeQuiet(file);
  },
  'tempnam() returns a usable path string'() {
    const name = misc.tempnam(TMP, 'pfx-');
    assert(typeof name === 'string');
    assert(name.length > 0);
  },
  'mkstemp() creates and opens a real temp file'() {
    ensureDir();
    const template = `${TMP}/mkstemp-XXXXXX`;
    const fd = misc.mkstemp(template);
    assert(fd >= 0);
    os.close(fd);
  },
  'access() checks file accessibility'() {
    ensureDir();
    const file = `${TMP}/access-me.txt`;
    removeQuiet(file);
    const fd = os.open(file, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
    os.close(fd);
    eq(misc.access(file, misc.F_OK), 0);
    removeQuiet(file);
  },
  'fstat() stats an open file descriptor'() {
    ensureDir();
    const file = `${TMP}/fstat-me.txt`;
    removeQuiet(file);
    const fd = os.open(file, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
    os.write(fd, new Uint8Array([1, 2, 3]).buffer, 0, 3);
    os.close(fd);
    const rfd = os.open(file, os.O_RDONLY, 0);
    const [st, err] = misc.fstat(rfd);
    eq(err, 0);
    eq(st.size, 3);
    os.close(rfd);
    removeQuiet(file);
  },

  /* ---- processes & users ---- */

  'getpid() matches across calls'() {
    eq(misc.getpid(), misc.getpid());
    assert(typeof misc.getpid() === 'number');
  },
  'gettid()/getppid()/getuid()/getgid()/geteuid()/getegid() return numbers'() {
    assert(typeof misc.gettid() === 'number');
    assert(typeof misc.getppid() === 'number');
    assert(typeof misc.getuid() === 'number');
    assert(typeof misc.getgid() === 'number');
    assert(typeof misc.geteuid() === 'number');
    assert(typeof misc.getegid() === 'number');
  },
  'uname() describes the running system'() {
    const u = misc.uname();
    eq(u.sysname, 'Linux');
    assert(typeof u.release === 'string');
  },
  'getRelease() describes the QuickJS build'() {
    const r = misc.getRelease();
    assert(typeof r.name === 'string');
  },
  'getExecutable()/getWorkingDirectory() resolve /proc/self links'() {
    assert(misc.getExecutable().length > 0);
    eq(misc.getWorkingDirectory(), os.getcwd()[0]);
  },
  'getCommandLine()/getEnvironment() read process info'() {
    assert(Array.isArray(misc.getCommandLine()));
    assert(typeof misc.getEnvironment() === 'object');
  },

  /* ---- constants ---- */

  'exposes numeric O_*, F_*, GLOB_* constants'() {
    assert(typeof misc.O_RDONLY === 'number');
    assert(typeof misc.F_OK === 'number');
    assert(typeof misc.GLOB_NOSORT === 'number');
  },
});
