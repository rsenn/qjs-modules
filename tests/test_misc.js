import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { Location } from 'misc';
import { extendArray } from 'util';
import * as misc from 'misc';
import * as fs from 'fs';

('use strict');
('use math');

extendArray(Array.prototype);

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      breakLength: 100,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: 0,
      showHidden: false
    }
  });
  console.log('console.options', console.options);
  let loc = new Location('test.js:12:1');
  console.log('loc', loc);
  loc = new Location('test.js', 12, 1);
  console.log('loc', loc);
  let loc2 = new Location(loc);
  console.log('loc2', loc2);

  console.log('loc.toString()', loc.toString());

let f = fs.readFileSync('/home/roman/Downloads/GBC_ROMS/SpongeBob SquarePants - Legend of the Lost Spatula (U).gbc', null);
let b = f.slice(0,1024) ?? misc.toArrayBuffer("TEST DATA");
let s = misc.btoa(b);
  console.log('b', b);

  console.log('misc.toArrayBuffer()',b );
  console.log('misc.btoa()', s);
  console.log('misc.atob()', misc.atob(s));
  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
