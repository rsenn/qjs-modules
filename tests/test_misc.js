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
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
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

  let f = fs.readFileSync('/home/roman/Downloads/GBC_ROMS/SpongeBob SquarePants - Legend of the Lost Spatula (U).gbc',
    null
  );
  let b = f.slice(0, 1024) ?? misc.toArrayBuffer('TEST DATA');
  let s = misc.btoa(b);
  console.log('b', b);

  console.log('misc.toArrayBuffer()', b);
  console.log('misc.btoa()', s);
  console.log('misc.atob()', misc.atob(s));
  let mod = misc.compileFile('lib/fs.js', true);
  let modfn = getModuleFunction(mod);
  let bc = misc.writeObject(mod);
  let fbc = misc.writeObject(modfn);
  let opcodes = misc.getOpCodes(true);
  console.log('misc.compileFile()', mod);
  console.log('getModuleFunction(mod)', modfn);
  console.log('misc.writeObject(mod)', bc);
  console.log('misc.writeObject(modfn)', fbc);
  fs.writeFileSync('bytecode.bin', bc);
  console.log('misc.readObject()', misc.readObject(bc));
  let fnbc = misc.getByteCode(main);
  console.log('misc.getByteCode()', fnbc);
  let ba = new Uint8Array(fnbc);
  let opcode;
  let i = 0;
  /*let bcver = ba[i++];
    let idx_to_atom_count, str;
    [idx_to_atom_count, i] = get_leb128(ba, i);

for(let j = 0; j < idx_to_atom_count; j++) {
    [str, i] = get_str(ba, i);
    console.log(j, { str});
}
console.log("i =",i);
  */

  try {
    for(; i < ba.length; i += opcode.size) {
      const code = ba[i];
      opcode = opcodes[code];

      console.log(i.toString(16).padStart(8, '0') + ': ',
        toHex(code),
        opcode.name.padEnd(32),
        ...[...ba.slice(i + 1, i + opcode.size)].map(n => toHex(n))
      );
    }
  } catch(e) {}
  console.log('ba.length', toHex(ba.length));
  console.log('misc.opcodes', opcodes);

  function toHex(num) {
    let r = num.toString(16);
    return r.padStart(2, '0');
  }
  function toHex32(num) {
    let ia;
    if(num < 0) ia = new Int32Array([num]);
    else ia = new Uint32Array([num]);
    return '0x' + ('00000000' + new Uint32Array(ia.buffer)[0].toString(16)).slice(-8);
  }

  function get_leb128(buf, offset, len) {
    let v,
      a,
      i,
      j = offset;
    len ??= buf.length;
    v = 0;
    for(i = 0; i < 5; i++) {
      if(j >= len) break;
      a = buf[j++];
      v |= (a & 0x7f) << (i * 7);
      if(!(a & 0x80)) {
        return [v, j];
      }
    }
    return [0, -1];
  }
  function get_bytes(buf, offset, n) {
    let v,
      a,
      i,
      j = offset;
    let len = buf.length;
    v = '';
    for(i = 0; i < n; i++) {
      if(j >= len) break;
      a = buf[j++];
      v += String.fromCharCode(a);
    }
    return [v, j];
  }

  function get_str(buf, offset) {
    let [str_len, j] = get_leb128(buf, offset);
    let is_wide = str_len & 1;
    str_len >>= 1;
    let str_size = str_len << is_wide;
    return get_bytes(buf, j, str_size);
  }
  console.log('misc.resizeArrayBuffer()', misc.resizeArrayBuffer(fnbc, 100));
  let max;

  console.log('valueToAtom()', (max = misc.valueToAtom('BLAH XXXX')));

  for(let atom = 0; atom <= 1000; atom++)
    console.log(`atom[${toHex32(atom)}] =`, misc.atomToValue(atom));
  for(let atom = 0x80000000; atom <= 0x800001ff; atom++)
    console.log(`atom[${toHex32(atom)}] =`, misc.atomToValue(atom));

  const Range = (from, to) => [...new Array(to - from).keys()].map(n => n + from);

  console.log('valueToAtom()', toHex32(misc.valueToAtom(3)));
  console.log('valueToAtom()', misc.valueToAtom(-3));
  console.log('atomToValue()', misc.atomToValue(1));
  console.log('misc.getClassID()', misc.getClassID({}));
  console.log('misc.getClassID()', misc.getClassID(new Console()));
  console.log('misc.getClassID()', misc.getClassID(new ArrayBuffer(1024)));
  console.log('misc.getClassID()', misc.getClassID(new Map()));
  console.log('misc.getClassID()', misc.getClassID(Symbol.for('quickjs.inspect.custom')));
  console.log('misc.getClassID()', misc.getClassID(Symbol));
  console.log('misc.getClassCount()', misc.getClassCount());
  console.log('misc.getClassName()',
    new Map(
      Range(1, misc.getClassCount())
        .map((id, idx) => [idx, [misc.getClassName(id), misc.getClassConstructor(id)]])
     )
  );

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
