/* json5.js: JSON5.parse and JSON5.stringify, Bun's JSON5 namespace (also the npm json5 shape).
 * depends on: json (JsonParser with dialect json5, JsonBuilder).
 * rule: Bun's results, checked against bun 1.4.2; a parse error is a SyntaxError
 * with the engine's code and loc, its message starts with "JSON5 Parse error: ".
 *
 *   JSON5.parse("{a: 1, 'b': [0x1F, .5,], // c\n}")   // { a: 1, b: [31, 0.5] }
 *   JSON5.stringify({ a: [1, 'x'] })                    // "{a:[1,'x']}"
 *   JSON5.stringify({ a: 1 }, null, 2)                  // "{\n  a: 1,\n}"
 */
import { JsonBuilder, JsonParser } from 'json';

/* the one value of text in dialect, or the SyntaxError with the prefix */
export function parseDialect(text, dialect, prefix) {
  const builder = new JsonBuilder({ plain: true });

  try {
    new JsonParser(String(text), { dialect }).pump(builder, true);
  } catch(e) {
    if(e instanceof SyntaxError) e.message = prefix + e.message;

    throw e;
  }

  return builder.take()[0];
}

/* parse(text): the value of a JSON5 document, one argument like Bun */
export function parse(text) {
  return parseDialect(text, 'json5', 'JSON5 Parse error: ');
}

const IDENTIFIER = /^[\p{ID_Start}$_][\p{ID_Continue}$\u200c\u200d]*$/u;

/* control characters as \0, \b, \t, \n, \f, \r or \xHH, like Bun */
const ESCAPES = { '\0': '\\0', '\b': '\\b', '\t': '\\t', '\n': '\\n', '\f': '\\f', '\r': '\\r', "'": "\\'", '\\': '\\\\', '\u2028': '\\u2028', '\u2029': '\\u2029' };

const quote = s => "'" + s.replace(/[\0-\x1f'\\\u2028\u2029]/g, c => ESCAPES[c] ?? '\\x' + c.charCodeAt(0).toString(16).padStart(2, '0')) + "'";

function number(n) {
  if(Number.isNaN(n)) return 'NaN';
  if(!Number.isFinite(n)) return n > 0 ? 'Infinity' : '-Infinity';

  return String(n);
}

/* stringify(value, replacer, space): JSON5 text, keys bare where they are identifiers
 *
 * no toJSON() calls and no replacer (null only), like Bun; space is a count up to 10
 * or a string of up to 10 characters, and indented output ends every member with a comma. */
export function stringify(value, replacer, space) {
  if(replacer != null) throw new Error('JSON5.stringify does not support the replacer argument');

  const indent = typeof space == 'number' ? ' '.repeat(Math.max(0, Math.min(10, Math.floor(space)))) : typeof space == 'string' ? space.slice(0, 10) : '';
  const stack = [];

  function write(v, prefix) {
    if(v instanceof Number || v instanceof String || v instanceof Boolean) v = v.valueOf();

    switch (typeof v) {
      case 'string':
        return quote(v);
      case 'number':
        return number(v);
      case 'boolean':
        return String(v);
      case 'bigint':
        throw new Error('JSON5.stringify cannot serialize BigInt');
      case 'undefined':
      case 'function':
      case 'symbol':
        return undefined;
    }

    if(v === null) return 'null';
    if(stack.includes(v)) throw new Error('Converting circular structure to JSON5');

    stack.push(v);

    const inner = prefix + indent;
    const items = Array.isArray(v)
      ? Array.from(v, x => write(x, inner) ?? 'null')
      : Object.keys(v).flatMap(k => {
          const s = write(v[k], inner);

          return s === undefined ? [] : [(IDENTIFIER.test(k) ? k : quote(k)) + (indent ? ': ' : ':') + s];
        });

    stack.pop();

    const [open, close] = Array.isArray(v) ? '[]' : '{}';

    if(!items.length) return open + close;
    if(!indent) return open + items.join(',') + close;

    return open + '\n' + items.map(s => inner + s + ',\n').join('') + prefix + close;
  }

  return write(value, '');
}

export const JSON5 = { parse, stringify };

export default JSON5;
