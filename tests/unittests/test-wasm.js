import * as wasm from 'wasm';
import { assert, eq, tests } from '../../lib/tinytest.js';

const HEAD = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

/* (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add) */
const ADD = new Uint8Array([
  ...HEAD, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61,
  0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b
]);

/* (import "env" "f" (func (param i32) (result i32)))
 * (func (export "g") (param i32) (result i32) local.get 0 call 0) */
const IMPORT = new Uint8Array([
  ...HEAD, 0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x02, 0x09, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x01, 0x66, 0x00,
  0x00, 0x03, 0x02, 0x01, 0x00, 0x07, 0x05, 0x01, 0x01, 0x67, 0x00, 0x01, 0x0a, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00,
  0x10, 0x00, 0x0b
]);

/* (memory (export "mem") 1) */
const MEMORY = new Uint8Array([...HEAD, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x07, 0x01, 0x03, 0x6d, 0x65, 0x6d, 0x02, 0x00]);

/* (table (export "t") 1 funcref) (elem (i32.const 0) $f) (func $f (result i32) i32.const 7) */
const TABLE = new Uint8Array([
  ...HEAD, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x04, 0x04, 0x01, 0x70, 0x00, 0x01, 0x07,
  0x05, 0x01, 0x01, 0x74, 0x01, 0x00, 0x09, 0x07, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x01, 0x00, 0x0a, 0x06, 0x01, 0x04,
  0x00, 0x41, 0x07, 0x0b
]);

/* (import "A" "mem" (memory 1)) (func (export "load") (result i32) i32.const 0 i32.load) */
const MEMIMPORT = new Uint8Array([
  ...HEAD, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x02, 0x0a, 0x01, 0x01, 0x41, 0x03, 0x6d, 0x65, 0x6d, 0x02, 0x00, 0x01,
  0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01, 0x04, 0x6c, 0x6f, 0x61, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x41,
  0x00, 0x28, 0x02, 0x00, 0x0b
]);

/* (import "e" "g" (global i32)) (func (export "f") (result i32) global.get 0) */
const GLOBALIMPORT = new Uint8Array([
  ...HEAD, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x02, 0x08, 0x01, 0x01, 0x65, 0x01, 0x67, 0x03, 0x7f, 0x00, 0x03, 0x02,
  0x01, 0x00, 0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00, 0x0a, 0x06, 0x01, 0x04, 0x00, 0x23, 0x00, 0x0b
]);

/* custom section "x-test" with payload 01 02 03 */
const CUSTOM = new Uint8Array([...HEAD, 0x00, 0x0a, 0x06, 0x78, 0x2d, 0x74, 0x65, 0x73, 0x74, 0x01, 0x02, 0x03]);

const standalone = wasm.backend == 'wasm3'; /* wamr cannot share Memory/Table/Global objects */

tests({
  'validate() accepts a module and rejects garbage'() {
    assert(wasm.validate(ADD));
    assert(!wasm.validate(new Uint8Array([1, 2, 3])));
  },
  'Module.exports() and Module.imports() list descriptors'() {
    const m = new wasm.Module(IMPORT);

    eq(JSON.stringify(wasm.Module.imports(m)), '[{"module":"env","name":"f","kind":"function"}]');
    eq(JSON.stringify(wasm.Module.exports(m)), '[{"name":"g","kind":"function"}]');
  },
  'new Module() throws CompileError on bad bytes'() {
    let err;

    try {
      new wasm.Module(new Uint8Array([0, 1, 2, 3]));
    } catch(e) {
      err = e;
    }
    assert(err instanceof wasm.CompileError);
  },
  'exported function is callable'() {
    const { add } = new wasm.Instance(new wasm.Module(ADD)).exports;

    eq(add(2, 3), 5);
    eq(add(0x7fffffff, 1), -0x80000000);
  },
  'imported JS function is called from wasm'() {
    const { g } = new wasm.Instance(new wasm.Module(IMPORT), { env: { f: x => x * 2 } }).exports;

    eq(g(21), 42);
  },
  'exception thrown by an import propagates'() {
    const { g } = new wasm.Instance(new wasm.Module(IMPORT), {
      env: {
        f() {
          throw new RangeError('boom');
        }
      }
    }).exports;
    let err;

    try {
      g(1);
    } catch(e) {
      err = e;
    }
    assert(err instanceof RangeError);
  },
  'missing import throws LinkError'() {
    let err;

    try {
      new wasm.Instance(new wasm.Module(IMPORT), { env: {} });
    } catch(e) {
      err = e;
    }
    assert(err instanceof wasm.LinkError);
  },
  'exported Memory exposes buffer and grow()'() {
    const { mem } = new wasm.Instance(new wasm.Module(MEMORY)).exports;

    eq(mem.buffer.byteLength, 65536);
    const old = mem.buffer;

    eq(mem.grow(1), 1);
    eq(mem.buffer.byteLength, 131072);
    eq(old.byteLength, 0);
  },
  'exported Table has length and get() returns callable functions'() {
    const { t } = new wasm.Instance(new wasm.Module(TABLE)).exports;

    eq(t.length, 1);
    eq(t.get(0)(), 7);
  },
  'classes carry a toStringTag and exports are frozen'() {
    const inst = new wasm.Instance(new wasm.Module(ADD));

    eq(Object.prototype.toString.call(inst), '[object WebAssembly.Instance]');
    eq(Object.prototype.toString.call(new wasm.Module(ADD)), '[object WebAssembly.Module]');
    assert(Object.isFrozen(inst.exports));
    eq(Object.getPrototypeOf(inst.exports), null);
  },
  'exported function has its function index as name and its arity as length'() {
    const { add } = new wasm.Instance(new wasm.Module(ADD)).exports;

    eq(add.name, '0');
    eq(add.length, 2);
  },
  'Module.customSections() returns the payloads'() {
    const m = new wasm.Module(CUSTOM);
    const [a] = wasm.Module.customSections(m, 'x-test');

    eq([...new Uint8Array(a)].join(), '1,2,3');
    eq(wasm.Module.customSections(m, 'other').length, 0);
  },
  'new Memory() is shared between instances'() {
    if(!standalone) return;
    const mem = new wasm.Memory({ initial: 1, maximum: 2 });
    const a = new wasm.Instance(new wasm.Module(MEMIMPORT), { A: { mem } }).exports;
    const b = new wasm.Instance(new wasm.Module(MEMIMPORT), { A: { mem } }).exports;

    new Uint32Array(mem.buffer)[0] = 42;
    eq(a.load(), 42);
    eq(b.load(), 42);
    eq(mem.grow(1), 1);
    eq(mem.buffer.byteLength, 131072);
  },
  'new Memory() validates its descriptor'() {
    let e1, e2;

    try {
      new wasm.Memory({});
    } catch(e) {
      e1 = e;
    }
    try {
      new wasm.Memory({ initial: 2, maximum: 1 });
    } catch(e) {
      e2 = e;
    }
    assert(e1 instanceof TypeError);
    assert(e2 instanceof RangeError);
  },
  'Table: set(), get(), grow() and length'() {
    if(!standalone) return;
    const { add } = new wasm.Instance(new wasm.Module(ADD)).exports;
    const t = new wasm.Table({ element: 'anyfunc', initial: 1, maximum: 3 });

    eq(t.length, 1);
    eq(t.get(0), null);
    t.set(0, add);
    eq(t.get(0)(2, 3), 5);
    eq(t.grow(1, add), 1);
    eq(t.length, 2);
    eq(t.get(1)(1, 1), 2);
    eq(t.grow(0), 2);
  },
  'Table.set() rejects a plain JS function'() {
    if(!standalone) return;
    const t = new wasm.Table({ element: 'anyfunc', initial: 1 });
    let err;

    try {
      t.set(0, () => 1);
    } catch(e) {
      err = e;
    }
    assert(err instanceof TypeError);
  },
  'Global: value, valueOf, mutability and i64'() {
    if(!standalone) return;
    const g = new wasm.Global({ value: 'i32', mutable: true }, 5);

    eq(g.value, 5);
    g.value = 7;
    eq(g.valueOf(), 7);

    const c = new wasm.Global({ value: 'i64' }, 5n);
    let err;

    eq(c.value, 5n);
    try {
      c.value = 1n;
    } catch(e) {
      err = e;
    }
    assert(err instanceof TypeError);
  },
  'Global imports: a Global object and a plain number'() {
    if(!standalone) return;
    const m = new wasm.Module(GLOBALIMPORT);

    eq(new wasm.Instance(m, { e: { g: new wasm.Global({ value: 'i32' }, 7) } }).exports.f(), 7);
    eq(new wasm.Instance(m, { e: { g: 9 } }).exports.f(), 9);
  }
});
