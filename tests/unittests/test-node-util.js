/* lib/node/util.js: Node's util on node:util (expected strings are Node 22 output) */
import { MIMEType, TextDecoder, diff, format, inspect, parseArgs, parseEnv, stripVTControlCharacters, styleText, types } from 'node:util';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'inspect: depth, grouping and class names'() {
    eq(inspect({ a: 1, b: { c: { d: { e: 1 } } } }), '{ a: 1, b: { c: { d: [Object] } } }');
    eq(inspect([1, 2, 3, [4, [5, [6]]]]), '[ 1, 2, 3, [ 4, [ 5, [Array] ] ] ]');
    eq(inspect(Array.from({ length: 8 }, (_, i) => i * 100)), '[\n    0, 100, 200, 300,\n  400, 500, 600, 700\n]');
    class A { constructor() { this.x = 1; } }
    eq(inspect(new A()), 'A { x: 1 }');
    eq(inspect(A), '[class A]');
  },
  'inspect: circular references, maps and sets'() {
    const o = {};
    o.a = [o];
    eq(inspect(o), '<ref *1> { a: [ [Circular *1] ] }');
    eq(inspect(new Map([[1, { a: 1 }]])), 'Map(1) { 1 => { a: 1 } }');
    eq(inspect(new Set([1, 'a'])), "Set(2) { 1, 'a' }");
  },
  'inspect: strings, numbers and options'() {
    eq(inspect("it's"), '"it\'s"');
    eq(inspect(-0), '-0');
    eq(inspect(1234567.5, { numericSeparator: true }), '1_234_567.5');
    eq(inspect({ b: 2, a: 1 }, { sorted: true }), '{ a: 1, b: 2 }');
    eq(inspect({ a: [1, 2] }, { compact: false }), '{\n  a: [\n    1,\n    2\n  ]\n}');
    eq(inspect('x', { colors: true }), "\x1b[32m'x'\x1b[39m");
    eq(inspect({ get a() { return 1; } }), '{ a: [Getter] }');
  },
  'inspect: custom inspect functions'() {
    eq(inspect({ [inspect.custom]: depth => `D${depth}` }), 'D2');
    eq(inspect({ a: { [inspect.custom]: () => ({ r: 1 }) } }), '{ a: { r: 1 } }');
  },
  'format: specifiers'() {
    eq(format('%s:%s', 'foo'), 'foo:%s');
    eq(format('%d %i %f %j %%', '42', '42.5', '1.5', { a: 1 }), '42 42 1.5 {"a":1} %');
    eq(format('%s', { a: { b: 1 } }), '{ a: [Object] }');
    eq(format(1, 2, 3), '1 2 3');
  },
  'types: brand checks'() {
    assert(types.isMap(new Map()) && !types.isMap({}));
    assert(types.isTypedArray(new Uint8Array(1)) && types.isUint8Array(new Uint8Array(1)) && !types.isUint16Array(new Uint8Array(1)));
    assert(types.isPromise(Promise.resolve()) && types.isRegExp(/x/) && types.isDate(new Date()));
    assert(types.isAsyncFunction(async () => {}) && types.isGeneratorFunction(function* () {}));
  },
  'parseArgs: values, short groups, tokens and errors'() {
    const options = { foo: { type: 'boolean', short: 'f' }, bar: { type: 'string', short: 'b' } };
    const r = parseArgs({ args: ['-fb', 'x', 'pos'], options, allowPositionals: true });
    eq(JSON.stringify(r), '{"values":{"foo":true,"bar":"x"},"positionals":["pos"]}');
    eq(parseArgs({ args: ['-f'], options, tokens: true }).tokens.length, 1);
    let err;
    try {
      parseArgs({ args: ['--nope'], options });
    } catch(e) {
      err = e;
    }
    eq(err.code, 'ERR_PARSE_ARGS_UNKNOWN_OPTION');
  },
  'parseEnv, stripVTControlCharacters, styleText'() {
    eq(JSON.stringify(parseEnv('A=1\nB="two words"\n# c\nC=\'x\'')), '{"A":"1","B":"two words","C":"x"}');
    eq(stripVTControlCharacters('\x1b[31mhi\x1b[0m'), 'hi');
    eq(styleText('red', 'x', { validateStream: false }), '\x1b[31mx\x1b[39m');
  },
  'MIMEType parses, normalises and serialises'() {
    const m = new MIMEType('TEXT/Html; Charset="utf-8"; q="a b"');
    eq([m.type, m.subtype, m.essence, m.params.get('q')].join('|'), 'text|html|text/html|a b');
    eq(String(m), 'text/html;charset=utf-8;q="a b"');
  },
  'TextDecoder: encoding names, fatal, streaming'() {
    eq(new TextDecoder().encoding, 'utf-8');
    eq(new TextDecoder('latin1').encoding, 'windows-1252');
    const d = new TextDecoder();
    eq(d.decode(new Uint8Array([0xe2, 0x82]), { stream: true }) + d.decode(new Uint8Array([0xac])), '€');
    let err;
    try {
      new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array([0xff]));
    } catch(e) {
      err = e;
    }
    eq(err.code, 'ERR_ENCODING_INVALID_ENCODED_DATA');
  },
  'diff: Myers edit script'() {
    eq(JSON.stringify(diff(['1', '2', '3'], ['1', '3', '4'])), '[[0,"1"],[1,"2"],[0,"3"],[-1,"4"]]');
    eq(diff('same', 'same').length, 0);
  },
});
