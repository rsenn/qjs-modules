/* bun/yaml.js: Bun.YAML, loaded on the first read of it.
 * depends on: yaml (native, parse needs it built with CYAML), textcode.
 * rule: Bun's names, argument shapes, results and error types, checked against bun 1.4.2.
 *
 * | Bun.YAML                       | here                                                  |
 * | ------------------------------ | ----------------------------------------------------- |
 * | parse(text)                    | yaml.parse, then merge keys (<<) expanded like Bun    |
 * | stringify(value, null, space)  | own writer: flow style without space, block with it   |
 *
 * ```js
 * YAML.parse('a: 1\nb: [x, true]');        // { a: 1, b: ['x', true] }
 * YAML.stringify({ a: [1, 'x y'] });       // '{a: [1,x y]}'
 * YAML.stringify({ a: [1, 'x y'] }, null, 2);
 * ```
 *
 * stringify throws Error for a BigInt or a replacer; a circular value throws TypeError where
 * Bun writes an anchor. undefined, functions and symbols are skipped in objects and arrays.
 */
import { parse as parseYaml } from 'yaml';
import { TextDecoder } from 'textcode';

const decoder = new TextDecoder();

/* the YAML 1.2 core words and numbers a plain scalar would read back as */
const KEYWORD = /^(?:~|[YyNn]|[Nn]ull|NULL|[Tt]rue|TRUE|[Ff]alse|FALSE|[Yy]es|YES|[Nn]o|NO|[Oo]n|ON|[Oo]ff|OFF)$/;
const NUMBER = /^[-+]?(?:\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|0[xo][0-9a-fA-F]+|\.(?:inf|Inf|INF))$|^\.(?:nan|NaN|NAN)$/;

/* a string that must be quoted: empty, edge space, control character, flow or comment
 * character, an indicator first, ": " or a trailing colon, a keyword or a number */
function needsQuote(s) {
  return (
    s === '' ||
    /^\s|\s$|[\x00-\x1f\x7f\x85\xa0\u2028\u2029,\[\]{}#]|^[-?:&*!|>'"%@`]|:(?:\s|$)/.test(s) ||
    KEYWORD.test(s) ||
    NUMBER.test(s)
  );
}

/* YAML double-quote escapes: \0 \a \b \t \n \v \f \r \e, \xHH for the other control characters,
 * \N \_ \L \P for U+0085 U+00A0 U+2028 U+2029 */
const ESCAPES = { 0: '0', 7: 'a', 8: 'b', 9: 't', 10: 'n', 11: 'v', 12: 'f', 13: 'r', 27: 'e', 0x22: '"', 0x5c: '\\', 0x85: 'N', 0xa0: '_', 0x2028: 'L', 0x2029: 'P' };

const quote = s =>
  '"' +
  s.replace(/[\x00-\x1f"\\\x7f\x85\xa0\u2028\u2029]/g, c => {
    const n = c.charCodeAt(0);

    return '\\' + (ESCAPES[n] ?? 'x' + n.toString(16).padStart(2, '0'));
  }) +
  '"';

const string = s => (needsQuote(s) ? quote(s) : s);

function scalar(v) {
  switch (typeof v) {
    case 'string':
      return string(v);
    case 'number':
      return Number.isNaN(v) ? '.nan' : v === Infinity ? '.inf' : v === -Infinity ? '-.inf' : Object.is(v, -0) ? '-0' : String(v);
    case 'boolean':
      return String(v);
    case 'bigint':
      throw new Error('YAML.stringify cannot serialize BigInt');
  }

  return 'null';
}

const skipped = v => v === undefined || typeof v == 'function' || typeof v == 'symbol';
const unbox = v => (v instanceof Number || v instanceof String || v instanceof Boolean ? v.valueOf() : v);
const isCollection = v => v !== null && typeof v == 'object';
const entries = v => Object.keys(v).filter(k => !skipped(v[k]));
const items = v => Array.from(v).filter(x => !skipped(x));

function enter(v, seen) {
  if(seen.has(v)) throw new TypeError('YAML.stringify: circular reference');

  seen.add(v);
}

/* {a: 1,b: [1,2]}: flow style, the comma has no space after it */
function flow(v, seen) {
  v = unbox(v);

  if(!isCollection(v)) return scalar(v);

  enter(v, seen);

  const out = Array.isArray(v) ? '[' + items(v).map(x => flow(x, seen)).join(',') + ']' : '{' + entries(v).map(k => `${string(k)}: ${flow(v[k], seen)}`).join(',') + '}';

  seen.delete(v);
  return out;
}

const empty = v => (Array.isArray(v) ? !items(v).length : !entries(v).length);

/* a collection as lines at depth, a sequence item or map value starts one level deeper */
function block(v, depth, unit, seen) {
  const ind = unit.repeat(depth);
  const lines = [];

  enter(v, seen);

  if(Array.isArray(v)) {
    for(const x of items(v).map(unbox)) {
      if(!isCollection(x)) lines.push(`${ind}- ${scalar(x)}`);
      else if(empty(x)) lines.push(`${ind}- ${Array.isArray(x) ? '[]' : '{}'}`);
      else lines.push(ind + '- ' + block(x, depth + 1, unit, seen).slice(unit.length * (depth + 1)));
    }
  } else {
    for(const k of entries(v)) {
      const x = unbox(v[k]);

      if(!isCollection(x)) lines.push(`${ind}${string(k)}: ${scalar(x)}`);
      else if(empty(x)) lines.push(`${ind}${string(k)}: \n${unit.repeat(depth + 1)}${Array.isArray(x) ? '[]' : '{}'}`);
      else lines.push(`${ind}${string(k)}: \n` + block(x, depth + 1, unit, seen));
    }
  }

  seen.delete(v);
  return lines.join('\n');
}

/** Bun.YAML.stringify(value, replacer, space): flow style, or block when space is 1-10 or a string. */
export function stringify(value, replacer, space) {
  if(replacer !== undefined && replacer !== null) throw new Error('YAML.stringify does not support the replacer argument');

  value = unbox(value);

  if(skipped(value)) return undefined;

  if(typeof space == 'number') space = ' '.repeat(Math.max(0, Math.min(10, Math.floor(space))));
  else if(typeof space == 'string') space = space.slice(0, 10);
  else space = '';

  if(!isCollection(value)) return scalar(value);

  if(!space || empty(value)) return flow(value, new Set());

  return block(value, 0, space, new Set());
}

/* the merge sources of a << value: a mapping or a sequence of mappings, else none */
function sources(v) {
  const list = Array.isArray(v) ? v : [v];

  return list.every(x => x !== null && typeof x == 'object' && !Array.isArray(x)) ? list : null;
}

/* YAML 1.1 merge keys, which Bun expands and CYAML does not: the keys of the sources are
 * added where << stood, the first source wins and the mapping's own keys win over all */
function merge(v, seen = new Set()) {
  if(v === null || typeof v != 'object' || seen.has(v)) return v;

  seen.add(v);

  for(const k of Object.keys(v)) merge(v[k], seen);

  const list = !Array.isArray(v) && Object.prototype.hasOwnProperty.call(v, '<<') ? sources(v['<<']) : null;

  if(!list) return v;

  const own = Object.keys(v);
  const merged = new Map();

  for(const src of list) for(const k of Object.keys(src)) if(!merged.has(k)) merged.set(k, src[k]);

  const values = own.map(k => [k, v[k]]);

  for(const k of own) delete v[k];

  for(const [k, x] of values) {
    if(k === '<<') {
      for(const [m, y] of merged) if(!own.includes(m)) v[m] = y;
    } else v[k] = x;
  }

  return v;
}

/** Bun.YAML.parse(text): the value of a document, an array for several. Text is coerced to a string. */
export function parse(text) {
  if(text instanceof ArrayBuffer || ArrayBuffer.isView(text)) text = decoder.decode(text);
  else if(typeof text != 'string') text = String(text);

  return merge(parseYaml(text));
}

export const YAML = { parse, stringify };

export default YAML;
