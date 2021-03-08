import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as deep from 'deep.so';
import * as xml from 'xml.so';
import Console from './console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}

const inspectOptions = {
  colors: true,
  showHidden: false,
  customInspect: true,
  showProxy: false,
  getters: false,
  depth: Infinity,
  maxArrayLength: 100,
  maxStringLength: 200,
  compact: 2,
  hideKeys: ['loc', 'range', 'inspect', Symbol.for('nodejs.util.inspect.custom')]
};
function main(...args) {
  console = new Console(inspectOptions);

  console.log('args:', args);

  let data = std.loadFile(args[0] ?? 'FM-Radio-Receiver-1.5V.xml', 'utf-8');

  console.log('data:', data);

  let result = xml.read(data);
  console.log('result:', inspect(result, inspectOptions));

  let found = deep.find(result, n => typeof n == 'object' && n != null && n.tagName == 'elements');

 
  console.log('found:', inspect(found, inspectOptions));

 
  console.log('array:', inspect([, , , , 4, 5, 6, , ,], inspectOptions));
  let testObj = {};

  deep.set(testObj, 'a.0.b.0.c\\.x.0', null);
  deep.unset(testObj, 'a.0.b.0');
  console.log('testObj: ' + inspect(testObj, inspectOptions));

  let out = new Map();

  //out.set = function(...args) { console.log("args:", args); }
  //  out.set('@', ['blah']);

  let flat = deep.flatten(result, out, deep.MASK_PRIMITIVE | deep.MASK_STRING && ~deep.MASK_OBJECT);
  console.log('flat:', flat);
  console.log('flat.keys():', [...flat.keys()]);
  console.log('deep.MASK_STRING:', deep.MASK_NUMBER);
  console.log('deep:', deep);

  let clone = [];

  for(let [pointer, value] of out) {
    deep.set(clone, pointer, value);
  }

  let node = deep.get(result, '2.children.0.children.3.children.8.children.13.children.20');
  console.log('get():', node);
  let path = deep.pathOf(result, node);
  console.log('pathOf():', path);

  let obj1 = {
    a: [undefined, 1, 1234n],
    b: 2,
    c: 3,
    d: 4,
    e: [NaN, true, false, Infinity, null]
  };
  let obj2 = {
    d: 4,
    c: 3,
    b: 2,
    a: [undefined, 1, 1234n],
    e: [NaN, true, false, Infinity, null]
  };

  console.log('equals():', deep.equals(obj1, obj2));
  for(let o of [obj1]) {
    let it = deep.iterate(o);
    console.log('it:', it);
    for(let item of it) console.log('item:', item);
  }
  console.log('deep.RETURN_PATH:', deep.RETURN_PATH);
  console.log('deep.RETURN_VALUE:', deep.RETURN_VALUE);
  console.log('deep.RETURN_VALUE_PATH:', deep.RETURN_VALUE_PATH);
  console.log('deep.RETURN_PATH_VALUE:', deep.RETURN_PATH_VALUE);
  console.log('find():',
    deep.find(obj1, n => n == Infinity, deep.RETURN_PATH_VALUE)
  );

  deep.forEach(obj2, (n, p) => console.log('deep.forEach', { n, p }));
  console.log('obj1:', obj1);
  console.log('clone():', deep.clone(result));
  console.log('select():',
    deep.select(
      result,
      node => typeof node == 'object' && node.tagName == 'wire',
      deep.RETURN_PATH_VALUE
    )
  );

  std.gc();
}

main(...scriptArgs.slice(1));
