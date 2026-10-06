import '../../lib/webassembly.js';
import WA, { WebAssembly as NS } from '../../lib/webassembly.js';
import * as wasm from 'wasm';
import { assert, eq, tests } from '../../lib/tinytest.js';

const HEAD = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

/* (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add) */
const ADD = new Uint8Array([
  ...HEAD, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61,
  0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b
]);

/* (import "env" "f" (func (param i32) (result i32))) (func (export "g") (param i32) (result i32) local.get 0 call 0) */
const IMPORT = new Uint8Array([
  ...HEAD, 0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x02, 0x09, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x01, 0x66, 0x00,
  0x00, 0x03, 0x02, 0x01, 0x00, 0x07, 0x05, 0x01, 0x01, 0x67, 0x00, 0x01, 0x0a, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00,
  0x10, 0x00, 0x0b
]);

/* the part of a fetch() Response the streaming functions read */
class FakeResponse {
  constructor(bytes, { type = 'application/wasm', status = 200 } = {}) {
    this.bytes = bytes;
    this.status = status;
    this.ok = status >= 200 && status < 300;
    this.headers = { get: name => (name.toLowerCase() == 'content-type' ? type : null) };
  }
  arrayBuffer() {
    return Promise.resolve(this.bytes.buffer.slice(this.bytes.byteOffset, this.bytes.byteOffset + this.bytes.byteLength));
  }
}

/* the error class a promise rejects with, or undefined when it resolves */
const rejection = p => p.then(() => undefined, e => e);

tests({
  'the global is installed and is the default export'() {
    assert(globalThis.WebAssembly === WA);
    assert(NS === WA);
    eq(Object.prototype.toString.call(WebAssembly), '[object WebAssembly]');
    assert(WebAssembly.Module === wasm.Module);
  },
  'operations are enumerable, classes are not, the global is hidden'() {
    eq(Object.keys(WebAssembly).sort().join(), 'compile,compileStreaming,instantiate,instantiateStreaming,validate');
    assert(!Object.getOwnPropertyDescriptor(globalThis, 'WebAssembly').enumerable);
    assert(!Object.getOwnPropertyDescriptor(WebAssembly, 'Module').enumerable);
  },
  'function lengths and names match the spec'() {
    eq([WebAssembly.compile.length, WebAssembly.instantiate.length, WebAssembly.validate.length, WebAssembly.compileStreaming.length].join(), '1,1,1,1');
    eq(WebAssembly.instantiate.name, 'instantiate');
  },
  async 'compile() resolves a Module'() {
    const m = await WebAssembly.compile(ADD);

    assert(m instanceof WebAssembly.Module);
  },
  async 'compile() never throws synchronously'() {
    let p;

    try {
      p = WebAssembly.compile();
    } catch(e) {
      p = undefined;
    }
    assert(p instanceof Promise);
    assert((await rejection(p)) instanceof TypeError);
    assert((await rejection(WebAssembly.compile(new Uint8Array([1, 2])))) instanceof WebAssembly.CompileError);
  },
  async 'instantiate(bytes) resolves { module, instance }'() {
    const { module, instance } = await WebAssembly.instantiate(ADD);

    assert(module instanceof WebAssembly.Module);
    eq(instance.exports.add(2, 3), 5);
  },
  async 'instantiate(module) resolves an Instance'() {
    const inst = await WebAssembly.instantiate(await WebAssembly.compile(ADD));

    assert(inst instanceof WebAssembly.Instance);
    eq(inst.exports.add(1, 1), 2);
  },
  async 'instantiate() passes the import object'() {
    const { instance } = await WebAssembly.instantiate(IMPORT, { env: { f: x => x * 2 } });

    eq(instance.exports.g(21), 42);
  },
  async 'instantiate() rejects with the right error class'() {
    assert((await rejection(WebAssembly.instantiate())) instanceof TypeError);
    assert((await rejection(WebAssembly.instantiate(ADD, 5))) instanceof TypeError);
    assert((await rejection(WebAssembly.instantiate(new Uint8Array([0, 1])))) instanceof WebAssembly.CompileError);
    assert((await rejection(WebAssembly.instantiate(IMPORT, { env: {} }))) instanceof WebAssembly.LinkError);
  },
  'validate() throws TypeError for a non-BufferSource'() {
    let err;

    try {
      WebAssembly.validate();
    } catch(e) {
      err = e;
    }
    assert(err instanceof TypeError);
    assert(WebAssembly.validate(ADD));
    assert(!WebAssembly.validate(new Uint8Array([1])));
  },
  async 'compileStreaming() and instantiateStreaming() read a Response'() {
    const m = await WebAssembly.compileStreaming(new FakeResponse(ADD));
    const { module, instance } = await WebAssembly.instantiateStreaming(Promise.resolve(new FakeResponse(ADD)));

    assert(m instanceof WebAssembly.Module);
    assert(module instanceof WebAssembly.Module);
    eq(instance.exports.add(4, 5), 9);
  },
  async 'streaming accepts a content type with parameters'() {
    assert((await WebAssembly.compileStreaming(new FakeResponse(ADD, { type: 'application/wasm; charset=binary' }))) instanceof WebAssembly.Module);
  },
  async 'streaming rejects a bad MIME type, a bad status and a non-Response'() {
    assert((await rejection(WebAssembly.compileStreaming(new FakeResponse(ADD, { type: 'text/plain' })))) instanceof TypeError);
    assert((await rejection(WebAssembly.compileStreaming(new FakeResponse(ADD, { status: 404 })))) instanceof TypeError);
    assert((await rejection(WebAssembly.instantiateStreaming(5))) instanceof TypeError);
  }
});
