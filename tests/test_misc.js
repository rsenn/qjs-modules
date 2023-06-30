import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import { Location } from 'location';
import { extendArray, format, waitFor } from 'util';
import { fnmatch, FNM_CASEFOLD, FNM_FILE_NAME, FNM_LEADING_DIR, FNM_NOESCAPE, FNM_NOMATCH, FNM_PATHNAME, FNM_PERIOD, glob, GLOB_ERR, GLOB_MARK, GLOB_NOSORT, GLOB_NOCHECK, GLOB_NOMATCH, GLOB_NOESCAPE, GLOB_ALTDIRFUNC, GLOB_BRACE, GLOB_NOMAGIC, GLOB_TILDE, GLOB_MAGCHAR, GLOB_NOSPACE, GLOB_ABORTED, JS_EVAL_FLAG_COMPILE_ONLY, JS_EVAL_TYPE_MODULE, JS_EVAL_TYPE_GLOBAL, toArrayBuffer, btoa, atob, valueToAtom, atomToValue, getClassConstructor, arrayToBitfield, bitfieldToArray, compileScript, writeObject, readObject, getByteCode, getOpCodes, resizeArrayBuffer, getClassID, getClassCount, getClassName } from 'misc';
import * as fs from 'fs';
import * as path from 'path';

('use strict');
('use math');

extendArray(Array.prototype);

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 8,
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

  let f = fs.readFileSync('../CMakeLists.txt', null);
  console.log('f', f);
  let b = f ? f?.slice(0, 1024) : toArrayBuffer('TEST DATA');
  let s = btoa(b);
  console.log('b', b);

  console.log('misc.toArrayBuffer()', b);
  console.log('misc.btoa()', s);
  console.log('misc.atob()', atob(s));
  try {
    console.log('process.argv[1]', process.argv[1]);
    let script = path.join(path.dirname(process.argv[1]), '..', 'lib/fs.js');
    console.log('script', script);

    let mod = compileScript(script, JS_EVAL_FLAG_COMPILE_ONLY);
    console.log('misc.compileScript()', mod);
    let modfn = getModuleFunction(mod);
    console.log('getModuleFunction(mod)', modfn);
    let bc = writeObject(mod);
    console.log('misc.writeObject(mod)', bc);
    let fbc = writeObject(modfn);
    console.log('misc.writeObject(modfn)', fbc);
    let opcodes = getOpCodes(true);

    //fs.writeFileSync('bytecode.bin', bc);
    console.log('misc.readObject()', readObject(bc));
    let fnbc = getByteCode(main);
    console.log('misc.getByteCode()', fnbc);
    let ba = new Uint8Array(fnbc);
    let opcode;
    let i = 0;

    try {
      for(; i < ba.length; i += opcode.size) {
        const code = ba[i];
        opcode = opcodes[code];
        const offset = i.toString(16).padStart(8, '0');
        console.log(offset + ': ', toHex(code), opcode.name.padEnd(32), ...[...ba.slice(i + 1, i + opcode.size)].map(n => toHex(n)));
      }
    } catch(e) {}

    console.log('ba.length', toHex(ba.length));
    console.log('misc.opcodes', opcodes);
    /*    console.log('misc.resizeArrayBuffer()', resizeArrayBuffer(fnbc, 100));*/
    let max;

    console.log('valueToAtom()', (max = valueToAtom('BLAH XXXX')));
    if(0) {
      for(let atom = 0; atom <= 1000; atom++) console.log(`atom[${toHex32(atom)}] =`, atomToValue(atom));
      for(let atom = 0x80000000; atom <= 0x800001ff; atom++) console.log(`atom[${toHex32(atom)}] =`, atomToValue(atom));
    }
    const Range = (from, to) => [...new Array(to - from).keys()].map(n => n + from);

    console.log('valueToAtom()', toHex32(valueToAtom(3)));
    console.log('valueToAtom()', valueToAtom(-3));
    console.log('atomToValue()', atomToValue(1));

    if(0) {
      console.log('misc.getClassID()', getClassID({}));
      console.log('misc.getClassID()', getClassID(new Console()));
      console.log('misc.getClassID()', getClassID(new ArrayBuffer(1024)));
      console.log('misc.getClassID()', getClassID(new Map()));
      console.log('misc.getClassID()', getClassID(Symbol.for('quickjs.inspect.custom')));
      console.log('misc.getClassID()', getClassID(Symbol));
      console.log('misc.getClassCount()', getClassCount());
      console.log('misc.getClassName()', new Map(Range(1, getClassCount()).map((id, idx) => [idx, [getClassName(id), getClassConstructor(id)]])));
    }
    let bits = arrayToBitfield([2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30], 2);
    let arr = bitfieldToArray(bits, 0);
    console.log('bitfield', { bits, arr });
  } catch(error) {
    console.log('ERROR', error + '', '\n' + error.stack);
  }

  console.log('format()', { s: format('string %s', 'abcd') });
  console.log('format()', format('JSON %j', { str: 'abcd', num: 1234, bool: true }));
  console.log('format()', format('number %d', 123));
  console.log('format()', format('integer %i', '0x4d2'));
  console.log('format()', format('float %f', '.3141592653589793e+01'));
  console.log('format()', format('object %o', { str: 'abcd', num: 1234, bool: true }));

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
    let v = 0,
      j = offset;
    len ??= buf.length;
    for(let i = 0; i < 5; i++) {
      if(j >= len) break;
      const a = buf[j++];
      v |= (a & 0x7f) << (i * 7);
      if(!(a & 0x80)) return [v, j];
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
