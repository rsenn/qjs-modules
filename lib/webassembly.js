/* webassembly.js: the global `WebAssembly` of Browser, Node, Bun and Deno,
 * built from the native `wasm` module (https://webassembly.github.io/spec/js-api/).
 * runs on qjsm only; importing it installs the global unless one exists.
 * rule: every async member returns a Promise and never throws synchronously.
 *
 * ```js
 * import 'webassembly';
 * const { instance } = await WebAssembly.instantiate(bytes, { env: { f() {} } });
 * instance.exports.add(1, 2);
 * ```
 *
 * not provided: Tag, Exception, JSTag (exception handling).
 */
import * as wasm from 'wasm';

const { Module, Instance, Memory, Table, Global, CompileError, LinkError, RuntimeError, validate } = wasm;

const isObject = v => v !== null && (typeof v == 'object' || typeof v == 'function');

/* compile: compiles bytes to a Module.
 *
 *   BufferSource  bytes  the binary
 *
 *   returns  Promise of a Module
 *   rejects  TypeError for a non-BufferSource; CompileError for a bad binary
 */
function compile(bytes) {
  return new Promise(resolve => resolve(new Module(bytes)));
}

/* instantiate: Node's/Browser's overloaded instantiate().
 *
 * ```js
 * await WebAssembly.instantiate(bytes, imports); // { module, instance }
 * await WebAssembly.instantiate(module, imports); // an Instance
 * ```
 *
 *   BufferSource|Module  source   binary to compile, or a compiled Module
 *   object               imports  { moduleName: { name: value } }, default none
 *
 *   rejects  TypeError (bad source or imports); CompileError; LinkError; RuntimeError (start function)
 */
function instantiate(source, imports = undefined) {
  return new Promise(resolve => {
    if(imports !== undefined && !isObject(imports)) throw new TypeError('second argument must be undefined or an Object');

    if(source instanceof Module) return resolve(new Instance(source, imports));

    const module = new Module(source);

    resolve({ module, instance: new Instance(module, imports) });
  });
}

/* the bytes of a fetch() Response that holds a wasm binary.
 * rejects with TypeError unless it is a Response-like with MIME type
 * application/wasm and an ok status. */
async function responseBytes(source) {
  const response = await source;

  if(!isObject(response) || typeof response.arrayBuffer != 'function' || !isObject(response.headers) || typeof response.headers.get != 'function')
    throw new TypeError('argument must be a Response or a Promise resolving to a Response');

  const type = (response.headers.get('content-type') ?? '').split(';')[0].trim().toLowerCase();

  if(type != 'application/wasm') throw new TypeError(`WebAssembly response has unsupported MIME type '${type}'`);
  if(!response.ok) throw new TypeError(`WebAssembly response has status code ${response.status}`);

  return response.arrayBuffer();
}

/* compileStreaming: compile() of a fetch() Response (or a promise of one). */
function compileStreaming(source) {
  return responseBytes(source).then(compile);
}

/* instantiateStreaming: instantiate() of a fetch() Response; resolves { module, instance }. */
function instantiateStreaming(source, imports = undefined) {
  return responseBytes(source).then(bytes => instantiate(bytes, imports));
}

const WebAssembly = {};

/* operations are enumerable, classes are not (as in V8 and JSC) */
for(const [name, value] of Object.entries({ compile, validate, instantiate, compileStreaming, instantiateStreaming }))
  Object.defineProperty(WebAssembly, name, { value, writable: true, enumerable: true, configurable: true });

for(const [name, value] of Object.entries({ Module, Instance, Table, Memory, Global, CompileError, LinkError, RuntimeError }))
  Object.defineProperty(WebAssembly, name, { value, writable: true, enumerable: false, configurable: true });

Object.defineProperty(WebAssembly, Symbol.toStringTag, { value: 'WebAssembly', configurable: true });

if(!('WebAssembly' in globalThis)) Object.defineProperty(globalThis, 'WebAssembly', { value: WebAssembly, writable: true, enumerable: false, configurable: true });

export { WebAssembly, compile, validate, instantiate, compileStreaming, instantiateStreaming, Module, Instance, Memory, Table, Global, CompileError, LinkError, RuntimeError };
export default WebAssembly;
