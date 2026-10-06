/* lib/wasm-loader.js: .wasm as an ES module, qjsm only */
import installWasmLoader from '../../lib/wasm-loader.js';
import * as wasm from 'wasm';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = import.meta.url.slice(7).replace(/[^/]*$/, '') + '../fixtures/wasm-loader/';
let hooks;

tests({
  'setup'() {
    hooks = installWasmLoader();
  },
  async 'a .wasm import resolves its imports and exposes its exports'() {
    const m = await import(`${dir}twice.wasm`);

    eq(m.twice(21), 42);
    eq(Object.keys(m).sort().join(), 'g,memory,twice');
  },
  async 'a global export holds its value'() {
    // WAMR does not export globals: the binding is present, the value undefined
    eq((await import(`${dir}twice.wasm`)).g, wasm.backend == 'wasm3' ? 7 : undefined);
  },
  async 'a memory export is a WebAssembly.Memory'() {
    assert((await import(`${dir}twice.wasm`)).memory instanceof wasm.Memory);
  },
  async 'a .wasm file is importable from JS with named imports'() {
    eq((await import(`${dir}uses.js`)).twice(4), 8);
  },
  async 'a missing import module rejects'() {
    let err;

    try {
      await import(`${dir}missing.wasm`);
    } catch(e) {
      err = e;
    }
    assert(err);
  },
  'teardown'() {
    hooks.deregister();
  },
});
