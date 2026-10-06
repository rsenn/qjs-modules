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
  }
});
