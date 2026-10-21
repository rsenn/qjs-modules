/* node.js: Node.js compatibility, loaded by qjsm at startup unless -N.
 * installs the loaders for node: names, node_modules and CommonJS, and the
 * globals Node has that QuickJS lacks.
 * depends on: loaders/node, loaders/packages, loaders/commonjs, globals,
 * lazyGlobal, and qjs-lws for the network globals when it is installed.
 * rule: a global that already exists is never replaced.
 *
 * ```js
 * import 'node';   // what qjsm does before the script runs
 * Buffer.from('hi'); global === globalThis; await fetch(url);
 * ```
 */
import 'loaders/node';
import 'loaders/packages';
import installCommonJS from 'loaders/commonjs';
import lazyGlobal from 'lazyGlobal';

installCommonJS();

/* the globals loaded on first read: the network ones of lib/bun.js from
 * qjs-lws when it is installed, and the rest Node has. Defined after
 * globals, whose lib/stream.js reads DOMException while it loads.
 *
 *   name                            module        enumerable
 *   fetch                           fetch         yes, as in Node
 *   Request, Response               serve
 *   Headers                         lws/headers
 *   FormData                        lws/formdata
 *   WebSocket                       websocket
 *   Buffer                          node/buffer
 *   File                            file
 *   Event, CustomEvent              dom
 *   DOMException                    dom
 *   WebAssembly                     webassembly
 */
const GLOBALS = [
  ['fetch', 'fetch', true],
  ['Request', 'serve'],
  ['Response', 'serve'],
  ['Headers', 'lws/headers'],
  ['FormData', 'lws/formdata'],
  ['WebSocket', 'websocket'],
  ['Buffer', 'node/buffer'],
  ['File', 'file'],
  ['Event', 'dom'],
  ['CustomEvent', 'dom'],
  ['DOMException', 'dom'],
  ['WebAssembly', 'webassembly'],
];

/* eagerly: URL, AbortController, TextEncoder, Blob, the streams, atob,
 * crypto, Intl, ... the qjs-modules ones that streams, timers and
 * readline work with */
requireModule('globals');

for(const [name, spec, enumerable = false] of GLOBALS) lazyGlobal(globalThis, name, spec, enumerable);

if(!('global' in globalThis)) Object.defineProperty(globalThis, 'global', { value: globalThis, writable: true, enumerable: true, configurable: true });
