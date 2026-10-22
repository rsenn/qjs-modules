/* jsonc.js: JSONC.parse, Bun's JSONC namespace: JSON with comments and trailing commas.
 * depends on: json5 (parseDialect), json (JsonParser with dialect jsonc).
 * rule: JSONC as VS Code defines it, which Bun matches except for three inputs it also
 * takes: single-quoted strings, hex numbers and text after the value. Those are a
 * SyntaxError here, so a truncated or corrupt file is not read as valid.
 *
 *   JSONC.parse('// settings\n{"a": [1, 2,],}')   // { a: [1, 2] }
 */
import { parseDialect } from './json5.js';

/* parse(text): the value of a JSONC document, one argument like Bun */
export function parse(text) {
  return parseDialect(text, 'jsonc', 'JSONC Parse error: ');
}

export const JSONC = { parse };

export default JSONC;
