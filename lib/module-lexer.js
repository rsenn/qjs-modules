/* module-lexer.js: parse(), the import and export scanner for JS sources.
 * depends on: lib/lexer/ecmascript.js (tokens), textcode (ArrayBuffer input).
 * runs on qjsm only, the lexer is the native lexer module.
 * rule: offsets are UTF-16 indices into the source, a range is [start, end).
 */
import ECMAScriptLexer from './lexer/ecmascript.js';
import { TextDecoder } from 'textcode';

/* parse: the imports and exports of a module, as records.
 *
 *   const { format, facade, imports, exports, error } = parse(source);
 *
 *   string|ArrayBuffer  source   the module text, an ArrayBuffer is UTF-8
 *
 *   returns  format   'module' with import/export statements or import.meta,
 *                     else 'commonjs'
 *            facade   true when nothing but import and export-from or
 *                     export-list statements
 *            imports  import records in source order, see doc/js/module-lexer.md
 *            exports  export records in source order
 *            error    { code, line, column } of the first lexing error,
 *                     the records before it are kept
 */
export function parse(source) {
  if(typeof source != 'string') source = new TextDecoder().decode(source);

  const { tokens, error } = tokenize(source);
  const result = { format: 'commonjs', facade: true, imports: [], exports: [] };

  for(let i = 0; i < tokens.length; ) {
    const t = tokens[i], end = statement(tokens, i, result);

    if(end >= 0) {
      i = end + 1;

      if(tokens[i]?.value == ';' && tokens[i].type == 'punctuator') i++;

      continue;
    }

    if(t.type == 'keyword' && t.value == 'import' && !member(tokens, i)) importExpression(tokens, i, result);

    if(!(t.type == 'punctuator' && t.value == ';')) result.facade = false;

    i++;
  }

  if(error) result.error = error;

  return result;
}

/* openers and the closer each one needs, a template ${ is a backquote */
const CLOSER = { '(': ')', '[': ']', '{': '}', '${': '}', '`': 'nestedEnd' };

/* line terminators, for the newline flag of a token */
const EOL = /[\n\r\u2028\u2029]/;

/* the significant tokens of source: no whitespace, comments or shebang.
 *
 *   { type, value, start, end, line, nl, depth }
 *
 * nl is true when a line terminator precedes the token. depth counts the
 * open brackets around it, an opener and its closer have the same depth.
 * Returns { tokens, error? }: the first error, lexing goes on after it
 * unless the lexer itself fails.
 */
function tokenize(source) {
  const lexer = new ECMAScriptLexer(source, 'module-lexer');
  const offset = utf16Offsets(source);
  const tokens = [],
    stack = [];
  let nl = false,
    error;

  try {
    for(;;) {
      const regex = /^(INITIAL|NESTED)$/.test(lexer.topState());
      const tok = lexer.nextToken();

      if(!tok) break;

      const { type, lexeme } = tok;

      if(type == 'whitespace' || type == 'comment' || type == 'shebang') {
        if(EOL.test(lexeme)) nl = true;

        continue;
      }

      const cp = tok.loc.charOffset;
      const t = { type, value: lexeme, start: offset(cp), end: offset(cp + tok.charLength), line: tok.loc.line, nl, depth: stack.length };

      nl = false;

      /* the regex rule did not match where a regex may start */
      if(regex && type == 'punctuator' && (lexeme == '/' || lexeme == '/=')) error ??= failure(source, 'UNTERMINATED_REGEX', t.start);

      if(type == 'templateLiteralHead' || type == 'templateLiteralPart') {
        stack.push('`');
      } else if(type == 'punctuator' && lexeme in CLOSER) {
        stack.push(lexeme);
      } else if(type == 'nestedEnd' || (type == 'punctuator' && (lexeme == ')' || lexeme == ']' || lexeme == '}'))) {
        if(CLOSER[stack.pop()] != (type == 'nestedEnd' ? type : lexeme)) error ??= failure(source, lexeme == '}' ? 'UNEXPECTED_BRACE' : 'UNEXPECTED_PAREN', t.start);

        t.depth = stack.length;
      }

      tokens.push(t);
    }
  } catch(e) {
    const pos = offset(lexer.charPos),
      c = source[pos];

    error ??= failure(
      source,
      lexer.topState() == 'TEMPLATE' || c == '`' ? 'UNTERMINATED_TEMPLATE_STRING' : c == '"' || c == "'" ? 'UNTERMINATED_STRING_LITERAL' : c == '/' ? 'UNTERMINATED_REGEX' : 'UNEXPECTED_CHARACTER',
      pos,
    );
  }

  if(!error && stack.length) {
    const open = stack[stack.length - 1];

    error = failure(source, open == '`' ? 'UNTERMINATED_TEMPLATE_STRING' : open == '(' || open == '[' ? 'UNTERMINATED_PAREN' : 'UNTERMINATED_BRACE', source.length);
  }

  return { tokens, error };
}

/* the lexer counts code points: map a code point index to a UTF-16 one,
 * the identity unless the source has characters outside the BMP.
 */
function utf16Offsets(source) {
  if(!/[\uD800-\uDBFF][\uDC00-\uDFFF]/.test(source)) return cp => cp;

  const map = [];

  for(let i = 0; i < source.length; ) {
    const c = source.charCodeAt(i);

    map.push(i);
    i += c >= 0xd800 && c < 0xdc00 && (source.charCodeAt(i + 1) & 0xfc00) == 0xdc00 ? 2 : 1;
  }

  map.push(source.length);
  return cp => map[cp];
}

/* an error record: code as merve names it, line and column 1-based */
function failure(source, code, pos) {
  const before = source.slice(0, pos);
  const lineStart = Math.max(before.lastIndexOf('\n'), before.lastIndexOf('\r')) + 1;

  return { code, line: (before.match(/\r\n|[\n\r\u2028\u2029]/g)?.length ?? 0) + 1, column: pos - lineStart + 1 };
}

/* true when tokens[i] is a property name: x.import, x?.import */
function member(tokens, i) {
  const prev = tokens[i - 1];

  return !!prev && prev.type == 'punctuator' && (prev.value == '.' || prev.value == '?.');
}

/* index of the closer of the opener at tokens[i], -1 when it is missing */
function closer(tokens, i) {
  const { depth } = tokens[i];

  for(let k = i + 1; k < tokens.length; k++) if(tokens[k].depth == depth) return k;

  return -1;
}

/* a top-level import or export statement at tokens[i], added to result.
 * Returns the index of its last token, or -1 when it is none.
 */
function statement(tokens, i, result) {
  const t = tokens[i];

  if(t.depth != 0 || t.type != 'keyword' || member(tokens, i)) return -1;
  if(t.value == 'import') return importStatement(tokens, i, result);
  if(t.value == 'export') return exportStatement(tokens, i, result);

  return -1;
}

/* import declarations:
 *
 *   import './side.js'
 *   import a, { b as c, 'd-e' as d } from './x.js' with { type: 'json' }
 *   import * as ns from './y.js'
 */
function importStatement(tokens, i, result) {
  const bindings = [];
  let j = i + 1;

  if(tokens[j]?.type != 'stringLiteral') {
    if(isName(tokens[j]) && (tokens[j + 1]?.value == ',' || tokens[j + 1]?.value == 'from')) {
      bindings.push({ imported: 'default', local: name(tokens[j]) });
      j += tokens[j + 1].value == ',' ? 2 : 1;
    }

    if(tokens[j]?.value == '*') {
      if(tokens[j + 1]?.value != 'as' || !isName(tokens[j + 2])) return -1;

      bindings.push({ imported: '*', local: name(tokens[j + 2]) });
      j += 3;
    } else if(tokens[j]?.value == '{') {
      const list = specifiers(tokens, j);

      if(!list) return -1;

      for(const [a, b] of list.items) bindings.push({ imported: name(a), local: name(b ?? a) });

      j = list.end + 1;
    }

    if(tokens[j]?.value != 'from' || tokens[j].type != 'keyword') return -1;

    j++;
  }

  const end = source(tokens, j, i, result, { kind: 'import-statement' });

  if(end < 0) return -1;

  result.imports[result.imports.length - 1].bindings = bindings;
  result.format = 'module';
  return end;
}

/* the specifier string at tokens[j] and the attributes after it: adds an
 * import record to result, its range starts at tokens[start].
 * Returns the index of the last token, or -1 when tokens[j] is no string.
 */
function source(tokens, j, start, result, fields) {
  const spec = tokens[j];

  if(spec?.type != 'stringLiteral') return -1;

  const record = { ...fields, path: string(spec.value), bindings: [] };
  let end = j;

  if((tokens[j + 1]?.value == 'with' || tokens[j + 1]?.value == 'assert') && tokens[j + 2]?.value == '{') {
    const close = closer(tokens, j + 2);

    if(close < 0) return -1;

    record.attributes = [tokens[j + 2].start, tokens[close].end];
    end = close;
  }

  Object.assign(record, { range: [tokens[start].start, tokens[end].end], pathRange: [spec.start + 1, spec.end - 1], line: tokens[start].line });
  result.imports.push(record);
  return end;
}

/* the { a, b as c } list at tokens[j]: { items: [[a, b?], ...], end },
 * end is the index of the }. Returns null for anything else.
 */
function specifiers(tokens, j) {
  const items = [];
  let k = j + 1;

  while(tokens[k] && !(tokens[k].value == '}' && tokens[k].depth == tokens[j].depth)) {
    const a = tokens[k];
    let b;

    if(!isName(a) && a.type != 'stringLiteral') return null;

    if(tokens[k + 1]?.value == 'as' && tokens[k + 1].type == 'keyword') {
      b = tokens[k + 2];

      if(!isName(b) && b?.type != 'stringLiteral') return null;

      k += 2;
    }

    items.push([a, b]);
    k++;

    if(tokens[k]?.value == ',') k++;
    else if(tokens[k]?.value != '}') return null;
  }

  return tokens[k] ? { items, end: k } : null;
}

/* export declarations:
 *
 *   export * from './x.js'                 export * as ns from './x.js'
 *   export { a, b as c } from './x.js'     export { a, b as c }
 *   export default <expression>            export default function f() {}
 *   export const { a, b: [c] } = o, d = 1  export function f() {}
 */
function exportStatement(tokens, i, result) {
  const t = tokens[i + 1];

  if(!t) return -1;

  if(t.value == '*') {
    let j = i + 2,
      as;

    if(tokens[j]?.value == 'as') {
      as = tokens[j + 1];

      if(!isName(as) && as?.type != 'stringLiteral') return -1;

      j += 2;
    }

    if(tokens[j]?.value != 'from') return -1;

    const end = source(tokens, j + 1, i, result, { kind: 'import-statement' });

    if(end < 0) return -1;

    const imp = reexport(result);
    const nameTok = as ?? t;

    result.exports.push({ kind: 'reexport', name: as ? name(as) : '*', local: null, imported: null, path: imp.path, importIndex: result.imports.length - 1, range: imp.range, nameRange: [nameTok.start, nameTok.end], line: nameTok.line });
    result.format = 'module';
    return end;
  }

  if(t.value == '{') {
    const list = specifiers(tokens, i + 1);

    if(!list) return -1;

    let end = list.end;

    if(tokens[end + 1]?.value == 'from') {
      end = source(tokens, end + 2, i, result, { kind: 'import-statement' });

      if(end < 0) return -1;

      const imp = reexport(result),
        importIndex = result.imports.length - 1;

      for(const [a, b] of list.items) {
        const as = b ?? a;

        result.exports.push({ kind: 'reexport', name: name(as), local: null, imported: name(a), path: imp.path, importIndex, range: imp.range, nameRange: [as.start, as.end], importedRange: [a.start, a.end], line: as.line });
      }
    } else {
      const range = [tokens[i].start, tokens[end].end];

      for(const [a, b] of list.items) {
        const as = b ?? a;

        result.exports.push({ kind: 'list', name: name(as), local: name(a), range, nameRange: [as.start, as.end], localRange: [a.start, a.end], line: as.line });
      }
    }

    result.format = 'module';
    return end;
  }

  let end;

  if(t.value == 'default' && t.type == 'keyword') {
    const decl = declaration(tokens, i + 2);
    const record = { kind: 'default', name: 'default', local: null, range: null, nameRange: [t.start, t.end], line: t.line };

    if(decl) {
      end = decl.end;

      if(decl.names[0]) {
        record.local = name(decl.names[0]);
        record.localRange = [decl.names[0].start, decl.names[0].end];
      }
    } else {
      end = expressionEnd(tokens, i + 2, false) - 1;
    }

    record.range = [tokens[i].start, tokens[end].end];
    result.exports.push(record);
  } else {
    const decl = declaration(tokens, i + 1) ?? variables(tokens, i + 1);

    if(!decl || !decl.names.length) return -1;

    end = decl.end;

    const range = [tokens[i].start, tokens[end].end];

    for(const n of decl.names) result.exports.push({ kind: 'declaration', name: name(n), local: name(n), range, nameRange: [n.start, n.end], localRange: [n.start, n.end], line: n.line });
  }

  result.format = 'module';
  result.facade = false;
  return end;
}

/* the import record just added by source(), marked as a re-export */
function reexport(result) {
  const imp = result.imports[result.imports.length - 1];

  imp.reexport = true;
  return imp;
}

/* function, async function, function*, class at tokens[j]:
 * { names: [nameToken?], end }, end is the index of the closing }.
 */
function declaration(tokens, j) {
  let k = j;

  if(tokens[k]?.value == 'async' && tokens[k + 1]?.value == 'function' && !tokens[k + 1].nl) k++;

  const kw = tokens[k]?.value;

  if(tokens[k]?.type != 'keyword' || (kw != 'function' && kw != 'class')) return null;

  k++;

  if(kw == 'function' && tokens[k]?.value == '*') k++;

  const names = tokens[k] && isBinding(tokens[k]) ? [tokens[k]] : [];

  for(; k < tokens.length; k++) {
    if(tokens[k].value == '{' && tokens[k].depth == tokens[j].depth) {
      const end = closer(tokens, k);

      return end < 0 ? null : { names, end };
    }
  }

  return null;
}

/* var, let, const declarators at tokens[j]: { names, end }, the names of
 * every binding, destructuring patterns included.
 */
function variables(tokens, j) {
  const kw = tokens[j];

  if(kw?.type != 'keyword' || !/^(var|let|const)$/.test(kw.value)) return null;

  const names = [];
  let k = j + 1;

  for(;;) {
    k = pattern(tokens, k, names);

    if(tokens[k]?.value == '=') k = expressionEnd(tokens, k + 1, true);

    if(tokens[k]?.value != ',' || tokens[k].depth != kw.depth) break;

    k++;
  }

  return { names, end: k - 1 };
}

/* the binding names of the pattern at tokens[k], pushed onto names:
 * a name, { a, b: c, [k]: d, ...e } or [a, , b = 1, ...c].
 * Returns the index after the pattern.
 */
function pattern(tokens, k, names) {
  const t = tokens[k];

  if(!t) return k;

  if(t.value == '{' || t.value == '[') {
    const close = closer(tokens, k),
      inner = t.depth + 1;

    if(close < 0) return tokens.length;

    for(let j = k + 1; j < close; ) {
      if(tokens[j].value == ',') {
        j++;
      } else if(tokens[j].value == '...') {
        j = pattern(tokens, j + 1, names);
      } else if(t.value == '[') {
        j = pattern(tokens, j, names);
      } else {
        const key = tokens[j],
          after = key.value == '[' ? closer(tokens, j) + 1 : j + 1;

        if(tokens[after]?.value == ':') {
          j = pattern(tokens, after + 1, names);
        } else {
          names.push(key);
          j = after;
        }
      }

      if(tokens[j]?.value == '=') while(j < close && !(tokens[j].depth == inner && tokens[j].value == ',')) j++;
    }

    return close + 1;
  }

  if(isBinding(t)) names.push(t);

  return k + 1;
}

/* statements that start a new statement after a line break */
const STATEMENT = /^(import|export|const|let|var|function|class|if|for|while|do|return|try|switch|throw)$/;

/* the index after the expression at tokens[k]: stops at a top-level ;
 * (or , when comma is true) and at a statement keyword on a new line.
 */
function expressionEnd(tokens, k, comma) {
  const { depth } = tokens[k - 1];

  for(let j = k; j < tokens.length; j++) {
    const t = tokens[j];

    if(t.depth < depth) return j;

    if(t.depth == depth) {
      if(t.type == 'punctuator' && (t.value == ';' || (comma && t.value == ','))) return j;
      if(j > k && t.nl && t.type == 'keyword' && STATEMENT.test(t.value)) return j;
    }
  }

  return tokens.length;
}

/* import(spec, options) and import.meta at tokens[i], anywhere in the code */
function importExpression(tokens, i, result) {
  const t = tokens[i],
    next = tokens[i + 1];

  if(next?.value == '.' && tokens[i + 2]?.value == 'meta') {
    result.imports.push({ kind: 'import-meta', path: null, bindings: [], range: [t.start, tokens[i + 2].end], line: t.line });
    result.format = 'module';
    return;
  }

  if(next?.value != '(') return;

  const close = closer(tokens, i + 1);

  /* import() {} is a method named import */
  if(close < 0 || tokens[close + 1]?.value == '{') return;

  const args = [[]];

  for(let k = i + 2; k < close; k++) {
    if(tokens[k].depth == next.depth + 1 && tokens[k].value == ',') args.push([]);
    else args[args.length - 1].push(tokens[k]);
  }

  const arg = args[0];

  if(!arg.length) return;

  const first = arg[0],
    last = arg[arg.length - 1];
  const record = { kind: 'dynamic-import', path: null, bindings: [], range: [t.start, tokens[close].end], pathRange: [first.start, last.end], callStart: next.start };

  if(arg.length == 1 && (first.type == 'stringLiteral' || first.type == 'templateLiteral')) {
    record.path = string(first.value);
    record.pathRange = [first.start + 1, first.end - 1];
  } else if(first.type == 'templateLiteralHead' && last.type == 'templateLiteralTail' && last.depth == first.depth) {
    record.glob = arg
      .filter(a => a.depth == first.depth && /^template/.test(a.type))
      .map(a => string(a.type == 'templateLiteralHead' ? a.value.slice(0, -1) : a.type == 'templateLiteralPart' ? '`' + a.value.slice(0, -1) : '`' + a.value))
      .join('*');
  }

  if(args[1]?.length) record.attributes = [args[1][0].start, args[1][args[1].length - 1].end];

  record.line = t.line;
  result.imports.push(record);
}

/* a token that can be an IdentifierName: a name after as, in { }, after . */
function isName(t) {
  return !!t && (t.type == 'identifier' || t.type == 'keyword' || t.type == 'booleanLiteral' || t.type == 'nullLiteral');
}

/* a token that can be a binding: an identifier, or a keyword the lexer
 * reports although it is a valid name (as, from, of, async, let, static)
 */
function isBinding(t) {
  return t.type == 'identifier' || (t.type == 'keyword' && /^(as|from|of|async|let|static|yield|await)$/.test(t.value));
}

/* the name a token stands for, quotes and escapes removed */
function name(t) {
  return t.type == 'stringLiteral' ? string(t.value) : unescape(t.value);
}

/* the value of a string or template literal lexeme, quotes included */
function string(lexeme) {
  return unescape(lexeme.slice(1, -1));
}

const ESCAPE = { b: '\b', f: '\f', n: '\n', r: '\r', t: '\t', v: '\v', 0: '\0' };

/* the characters escape sequences stand for: \n \x41 A \u{1F600}, a
 * backslash before a line terminator is a line continuation
 */
function unescape(s) {
  if(!s.includes('\\')) return s;

  return s.replace(/\\(?:u\{([0-9a-fA-F]+)\}|u([0-9a-fA-F]{4})|x([0-9a-fA-F]{2})|(\r\n|[\n\r\u2028\u2029])|([^]))/g, (m, cp, u, x, eol, c) =>
    cp || u || x ? String.fromCodePoint(parseInt(cp || u || x, 16)) : eol ? '' : (ESCAPE[c] ?? c),
  );
}

export default parse;
