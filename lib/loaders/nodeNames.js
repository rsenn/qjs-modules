/* loaders/nodeNames.js: the names of Node's builtin modules, each also
 * valid with the node: prefix, plus vfs (node 26.4+). Shared by the node:
 * resolve hook of lib/loaders/node.js and require() of lib/require.js.
 *
 *   NODE.has('fs/promises')   // true
 *   NODE.has('sqlite')        // false: node:sqlite only, never bare
 */
const NODE = new Set(
  (
    'assert assert/strict async_hooks buffer child_process cluster console constants crypto dgram diagnostics_channel dns ' +
    'dns/promises domain events fs fs/promises http http2 https inspector inspector/promises module net os path ' +
    'path/posix path/win32 perf_hooks process punycode querystring readline readline/promises repl stream ' +
    'stream/consumers stream/promises stream/web string_decoder sys timers timers/promises tls trace_events tty url ' +
    'util util/types v8 vm vfs wasi worker_threads zlib'
  ).split(' '),
);

export default NODE;
