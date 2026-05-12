# quickjs-modules

Some modules for QuickJS

---

## arraybuffer_sink

Collects streamed writes into a single ArrayBuffer.

```js
import { ArrayBufferSink } from 'arraybuffer_sink';

const sink = new ArrayBufferSink();
sink.write(new Uint8Array([1, 2, 3]));
sink.write(new Uint8Array([4, 5, 6]));
const result = sink.toArrayBuffer(); // → ArrayBuffer with all written data
```

---

## child_process

Spawn subprocesses.

```js
import { exec, spawn, execSync } from 'child_process';

// Synchronous — returns { stdout, stderr, status }
const { stdout, stderr, status } = execSync('ls -la');
console.log(stdout);

// Spawn (async) — returns ChildProcess handle
const proc = spawn('grep', ['-r', 'pattern', '.']);
```

---

## deep

Deep object traversal using JSON Pointer–style paths (`'/a/b/c'`).

```js
import { find, get, set, unset } from 'deep';

const obj = { a: { b: { c: 42 } } };
get(obj, '/a/b/c');              // 42
set(obj, '/a/b/d', 'hello');
unset(obj, '/a/b/c');
find(obj, (val, key) => val === 42);   // returns first matching value
```

---

## directory

Directory class for iterating filesystem entries.

```js
import { Directory } from 'directory';

const dir = new Directory('.');
let entry;
while ((entry = dir.read()) !== null) {
  console.log(entry.name, entry.type);
}
dir.close();

// Or use entries() iterator:
for (const entry of new Directory('.').entries()) {
  console.log(entry.name);
}
```

---

## inspect

Pretty-print any JS value (like Node's `util.inspect`).

```js
import { inspect } from 'inspect';

console.log(inspect(obj, { depth: 4, colors: true, compact: false }));
```

Options: `depth`, `colors`, `compact`, `maxArrayLength`, `breakLength`.

---

## list

Doubly-linked list with iterator support.

```js
import { List } from 'list';

const lst = new List();
lst.push('a');
lst.push('b');
lst.unshift('z');           // prepend

for (const item of lst) console.log(item);

lst.pop();                  // remove from tail
lst.shift();                // remove from head
lst.length;                 // number of items
```

---

## location

Source location (filename + line + column), useful for error reporting.

```js
import { Location } from 'location';

const loc = new Location('file.js', 10, 4);
console.log(loc.file, loc.line, loc.column);
console.log(String(loc));   // "file.js:10:4"
```

---

## mmap

Memory-map files directly into an ArrayBuffer.

```js
import { mmap, munmap, PROT_READ, PROT_WRITE, MAP_SHARED, MAP_PRIVATE } from 'mmap';

const buf = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
// use buf as ArrayBuffer
munmap(buf);
```

---

## path

Path manipulation and filesystem queries.

```js
import * as path from 'path';

path.join('a', 'b', 'c.js')      // 'a/b/c.js'
path.basename('/foo/bar.txt')     // 'bar.txt'
path.dirname('/foo/bar.txt')      // '/foo'
path.extname('file.txt')          // '.txt'
path.resolve('rel/path')          // absolute path
path.normalize('a//b/../c')       // 'a/c'
path.relative('/from', '/to')
path.isAbsolute('/abs')           // boolean
path.isDirectory('/some/dir')     // boolean
path.isSymlink('/link')           // boolean
path.exists('/some/path')         // boolean
path.getcwd()                     // cwd string
path.gethome()                    // home dir string
path.realpath('.')                // canonicalized absolute path
path.readlink('/link')            // symlink target
path.components('/a/b/c')        // ['a','b','c']
path.sep                          // '/'
path.delimiter                    // ':'
```

---

## pointer

JSON Pointer wrapper — references a location within an object graph.

```js
import { Pointer } from 'pointer';

const p = new Pointer('/a/b/c');
const p2 = new Pointer(['a', 'b', 'c']);   // same thing

String(p)          // '/a/b/c'
p.toArray()        // ['a','b','c']
```

---

## predicate

Composable callable predicates — think of them as lazy, composable boolean functions.
`Predicate` instances are callable: `pred(value)` returns the result.

```js
import { Predicate } from 'predicate';

// Factory functions on Predicate:
const isString  = Predicate.type(0x04);          // TYPE_STRING
const matches   = Predicate.regexp(/^foo/);
const notNull   = Predicate.not(Predicate.equal(null));
const hasName   = Predicate.has('name');
const namePred  = Predicate.property('name', Predicate.string('alice'));
const both      = Predicate.and(isString, matches);
const either    = Predicate.or(isString, Predicate.type(0x02));
const everyItem = Predicate.every(isString);      // tests array elements
const someItem  = Predicate.some(isString);

// Evaluate (Predicate is also callable directly):
const p = Predicate.string('hello');
p('hello')     // true
p.eval('hi')   // false
p.call('hi')   // alias for eval

// Arithmetic predicates (for numeric pipelines):
const addFive = Predicate.add(Predicate.function(x => x), 5);
```

**Factory methods** (all return a `Predicate`):

| Method | Description |
|--------|-------------|
| `Predicate.type(flags)` | Match by JS type flags |
| `Predicate.charset(str)` | Match character in set |
| `Predicate.string(str)` | Match exact string |
| `Predicate.regexp(re)` | Match against RegExp |
| `Predicate.equal(val)` | Strict equality |
| `Predicate.not(pred)` | Logical NOT |
| `Predicate.notnot(pred)` | Double-NOT (truthy coercion) |
| `Predicate.and(...preds)` | Logical AND |
| `Predicate.or(...preds)` | Logical OR |
| `Predicate.xor(...preds)` | Logical XOR |
| `Predicate.instanceOf(ctor)` | `instanceof` check |
| `Predicate.prototypeIs(proto)` | Prototype identity check |
| `Predicate.has(prop)` | Has own property |
| `Predicate.property(prop[, pred])` | Get property, optionally test with pred |
| `Predicate.member(obj[, pred])` | Value is member of obj |
| `Predicate.function(fn[, thisObj])` | Wrap arbitrary function |
| `Predicate.some(pred)` | True if any array element matches |
| `Predicate.every(pred)` | True if all array elements match |
| `Predicate.shift(n, pred)` | Shift args by n before applying pred |
| `Predicate.slice(start, end)` | Slice args |
| `Predicate.index(pos[, pred])` | Test element at index |
| `Predicate.add/sub/mul/div/mod/bor/band/pow(a, b)` | Arithmetic predicates |

**Instance properties/methods**:
- `pred.id` — integer predicate type ID
- `pred.length` — arity (number of expected args)
- `pred.eval(...args)` — evaluate predicate
- `pred.call(...args)` — alias for eval
- `pred.keys()` — internal key enumeration
- `pred.values()` — internal value enumeration
- `pred.toString()` — human-readable description
- `pred.toSource()` — reconstructable source form

---

## queue

FIFO queue.

```js
import { Queue } from 'queue';

const q = new Queue();
q.push('a');
q.push('b');
q.shift();          // 'a'
q.size;             // current length
q.empty;            // boolean
```

---

## repeater

Async repeater — bridges push-based sources into async iterators.

```js
import { Repeater } from 'repeater';

const rpt = new Repeater((push, stop) => {
  push('value1');
  push('value2');
  stop();
});

for await (const val of rpt) {
  console.log(val);
}
```

---

## sockets

Low-level BSD socket API.

```js
import * as sockets from 'sockets';

const fd = sockets.socket(sockets.AF_INET, sockets.SOCK_STREAM, 0);
sockets.connect(fd, { address: '127.0.0.1', port: 8080 });
sockets.send(fd, 'hello');
const data = sockets.recv(fd, 1024);
sockets.close(fd);

// Server side:
const srv = sockets.socket(sockets.AF_INET, sockets.SOCK_STREAM, 0);
sockets.bind(srv, { address: '0.0.0.0', port: 9000 });
sockets.listen(srv, 5);
const client = sockets.accept(srv);
```

Constants: `AF_INET`, `AF_INET6`, `AF_UNIX`, `SOCK_STREAM`, `SOCK_DGRAM`, `SOCK_RAW`.

---

## syscallerror

A `SyscallError` class that extends `Error` with POSIX errno info.

```js
import { SyscallError } from 'syscallerror';

try {
  // ... some OS-level operation
} catch(e) {
  if (e instanceof SyscallError) {
    console.log(e.syscall);   // e.g. 'open'
    console.log(e.errno);     // numeric errno
    console.log(e.code);      // e.g. 'ENOENT'
    console.log(e.message);   // human-readable string
  }
}
```

---

## textcode

Text encoding and decoding (UTF-8 and other encodings).

```js
import { TextEncoder, TextDecoder, encode, decode } from 'textcode';

// Encode string → Uint8Array
const encoder = new TextEncoder();
const bytes = encoder.encode('hello');    // Uint8Array

// Or use free function:
const bytes2 = encode('hello');

// Decode Uint8Array / ArrayBuffer → string
const decoder = new TextDecoder('utf-8');
const str = decoder.decode(bytes);        // 'hello'

// Or use free function:
const str2 = decode(bytes);
```

---

## tree_walker

Walk arbitrary object/tree structures with depth-first traversal.

```js
import { TreeWalker, TreeIterator } from 'tree_walker';

const walker = new TreeWalker(rootNode);
while (walker.nextNode()) {
  console.log(walker.currentNode);
}

// Or as iterator:
for (const node of new TreeIterator(rootNode)) {
  console.log(node);
}
```

Flags control which nodes are visited (similar to DOM `TreeWalker` whatToShow).

---

## virtual

Virtual/proxy object wrapper — wraps an object with intercepted property access.

```js
import { Virtual } from 'virtual';

const vobj = new Virtual(target, {
  get(obj, prop) { /* intercept reads */ },
  set(obj, prop, val) { /* intercept writes */ },
});
```

---

## xml

Parse and serialize XML.

```js
import { read, write } from 'xml';

const doc = read('<root><item id="1">hello</item></root>');
// doc is a JS object tree

const xmlStr = write(doc);
console.log(xmlStr);
```
