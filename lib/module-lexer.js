/* module-lexer.js: parse(), print(), merge(), the import and export
 * scanner for JS sources and the printer of its import records.
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
 *            exports  export records in source order, ES and CommonJS
 *            error    { code, line, column } of the first lexing error,
 *                     the records before it are kept
 *
 * Both detections always run: require calls and merve's CommonJS export
 * patterns are reported in a module too.
 */
export function parse(source) {
  if(typeof source != 'string') source = new TextDecoder().decode(source);

  const { tokens, error } = tokenize(source);
  const result = { format: 'commonjs', facade: true, imports: [], exports: [] };
  const cjs = { calls: new Map(), bindings: new Map(), stars: new Map() };

  for(let i = 0; i < tokens.length; ) {
    const t = tokens[i], end = statement(tokens, i, result);

    if(end >= 0) {
      /* export function f() { require('x') } */
      while(++i <= end) expression(tokens, i, result, cjs);

      if(tokens[i]?.value == ';' && tokens[i].type == 'punctuator') i++;

      continue;
    }

    expression(tokens, i, result, cjs);

    if(!(t.type == 'punctuator' && t.value == ';')) result.facade = false;

    i++;
  }

  if(error) result.error = error;

  return result;
}

/* print: the statements an import record stands for, as source text.
 *
 *   print({ path: './x.js', bindings: [{ imported: 'b', local: 'c' }] })
 *     // "import { b as c } from './x.js';"
 *   print(record, 'commonjs')
 *     // "const { b: c } = require('./x.js');"
 *
 *   object  record  an import record, only path and bindings are read
 *   string  format  'module' (default) or 'commonjs'
 *
 *   returns  one statement, or two joined by a newline where one cannot
 *            hold all bindings: import * as a and { b } from one path
 *   throws   TypeError for a record without a path or with attributes
 */
export function print(record, format = 'module') {
  const { path, bindings } = record;

  if(typeof path != 'string' || record.attributes) throw new TypeError(`print: cannot print ${record.kind} record${record.attributes ? ' with attributes' : ''}`);

  const spec = quote(path);
  const def = bindings.find(b => b.imported == 'default');
  const stars = bindings.filter(b => b.imported == '*');
  const named = bindings.filter(b => b != def && b.imported != '*');
  const out = [];

  if(format == 'commonjs') {
    if(!bindings.length) return `require(${spec});`;

    for(const b of [def, ...stars]) if(b) out.push(`const ${b.local} = require(${spec});`);

    if(named.length) out.push(`const { ${named.map(b => (b.imported == b.local ? b.local : `${key(b.imported)}: ${b.local}`)).join(', ')} } = require(${spec});`);

    return out.join('\n');
  }

  if(!bindings.length) return `import ${spec};`;

  /* import a, * as b holds one namespace, import a, { c } the named ones */
  const clause = [def?.local, stars.length ? `* as ${stars[0].local}` : named.length ? `{ ${named.map(specifierText).join(', ')} }` : undefined];

  out.push(`import ${clause.filter(Boolean).join(', ')} from ${spec};`);

  for(const b of stars.slice(1)) out.push(`import * as ${b.local} from ${spec};`);

  if(stars.length && named.length) out.push(`import { ${named.map(specifierText).join(', ')} } from ${spec};`);

  return out.join('\n');
}

/* merge: one import-statement record per path, the bindings of all.
 *
 *   merge(parse("import { a } from 'x'; import { b } from 'x'").imports)
 *     // [{ kind: 'import-statement', path: 'x', bindings: [a, b], ... }]
 *
 * A merged record keeps the range, pathRange and line of the first one.
 * Re-exports, records with attributes and other kinds are not merged,
 * they stay where they are in the list. A binding repeated with the same
 * imported and local name is kept once.
 */
export function merge(imports) {
  const byPath = new Map(),
    result = [];

  for(const r of imports) {
    if(r.kind != 'import-statement' || r.reexport || r.attributes) {
      result.push(r);
      continue;
    }

    const first = byPath.get(r.path);

    if(!first) {
      const copy = { ...r, bindings: [...r.bindings] };

      byPath.set(r.path, copy);
      result.push(copy);
      continue;
    }

    for(const b of r.bindings) if(!first.bindings.some(o => o.imported == b.imported && o.local == b.local)) first.bindings.push(b);
  }

  return result;
}

/* a specifier string in single quotes: ./x.js gives './x.js' */
function quote(s) {
  return "'" + s.replace(/[\\'\n\r\u2028\u2029]/g, c => '\\' + (c == '\n' ? 'n' : c == '\r' ? 'r' : c > '~' ? 'u' + c.charCodeAt(0).toString(16) : c)) + "'";
}

/* an imported or exported name: bare when it is an identifier, else quoted */
function key(n) {
  return /^[\p{ID_Start}$_][\p{ID_Continue}$‌‍]*$/u.test(n) ? n : quote(n);
}

/* b, or a as b, of an import { } list */
function specifierText(b) {
  return b.imported == b.local ? b.local : `${key(b.imported)} as ${b.local}`;
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
 * every binding, destructuring patterns included. each(start, init) runs
 * per declarator with an initializer: the index of its pattern and of the
 * first token after the =.
 */
function variables(tokens, j, each) {
  const kw = tokens[j];

  if(kw?.type != 'keyword' || !/^(var|let|const)$/.test(kw.value)) return null;

  const names = [];
  let k = j + 1;

  for(;;) {
    const start = k;

    k = pattern(tokens, k, names);

    if(tokens[k]?.value == '=') {
      each?.(start, k + 1);
      k = expressionEnd(tokens, k + 1, true);
    }

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

/* import(), import.meta and the CommonJS patterns at tokens[i] */
function expression(tokens, i, result, cjs) {
  const t = tokens[i];

  if(t.type == 'keyword' && t.value == 'import' && !member(tokens, i)) importExpression(tokens, i, result);
  else commonjs(tokens, i, result, cjs);
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

  const call = callArguments(tokens, i + 1);

  if(!call || !call.args[0].length) return;

  const { close, args } = call;
  const record = { kind: 'dynamic-import', path: null, bindings: [], range: [t.start, tokens[close].end], callStart: next.start };

  specifier(record, args[0]);

  if(args[1]?.length) record.attributes = [args[1][0].start, args[1][args[1].length - 1].end];

  record.line = t.line;
  result.imports.push(record);
}

/* the arguments of the call whose ( is tokens[open]: { close, args },
 * args holds the tokens of each argument. Returns null when tokens[open]
 * is no (, or for f() {}, a method named f.
 */
function callArguments(tokens, open) {
  if(tokens[open]?.value != '(') return null;

  const close = closer(tokens, open);

  if(close < 0 || tokens[close + 1]?.value == '{') return null;

  const args = [[]];

  for(let k = open + 1; k < close; k++) {
    if(tokens[k].depth == tokens[open].depth + 1 && tokens[k].value == ',') args.push([]);
    else args[args.length - 1].push(tokens[k]);
  }

  /* f(a,) has one argument */
  if(args.length > 1 && !args[args.length - 1].length) args.pop();

  return { close, args };
}

/* sets path, pathRange and glob of record from the tokens of a specifier
 * argument: a literal has a path, a template with substitutions a glob
 */
function specifier(record, arg) {
  const first = arg[0],
    last = arg[arg.length - 1];

  record.pathRange = [first.start, last.end];

  if(arg.length == 1 && (first.type == 'stringLiteral' || first.type == 'templateLiteral')) {
    record.path = string(first.value);
    record.pathRange = [first.start + 1, first.end - 1];
  } else if(first.type == 'templateLiteralHead' && last.type == 'templateLiteralTail' && last.depth == first.depth) {
    record.glob = arg
      .filter(a => a.depth == first.depth && /^template/.test(a.type))
      .map(a => string(a.type == 'templateLiteralHead' ? a.value.slice(0, -1) : a.type == 'templateLiteralPart' ? '`' + a.value.slice(0, -1) : '`' + a.value))
      .join('*');
  }
}

/* CommonJS: require calls and merve 1.2.2's export patterns at tokens[i].
 *
 *   token                  pattern                                depth
 *   var, let, const        x = require(...), { a, b: c } = ...    0
 *   require                require(x), require.resolve(x)         any
 *   exports, module        exports.x = , module.exports = { }     any
 *   Object                 Object.defineProperty(exports, ...)    any
 *   Object                 Object.keys(x).forEach(...)            0
 *   __export(Star)         __exportStar(require(...))             0
 *
 * Like merve: no scope analysis, a local named require still counts.
 * cjs holds what spans tokens: the import index per require token
 * (calls), the bindings of a declared require (bindings) and the require
 * token per name declared as one (stars, for Object.keys).
 */
function commonjs(tokens, i, result, cjs) {
  const t = tokens[i];

  if(t.type == 'keyword') {
    if(t.depth == 0 && /^(var|let|const)$/.test(t.value) && !member(tokens, i)) variables(tokens, i, (start, init) => declarator(tokens, start, init, cjs));

    return;
  }

  if(t.type != 'identifier') return;

  /* tslib.__exportStar( counts too */
  if(t.value == '__export' || t.value == '__exportStar') {
    if(t.depth == 0 && tokens[i + 1]?.value == '(') {
      const close = closer(tokens, i + 1);

      requireReexport(tokens, i + 2, [t.start, tokens[close < 0 ? tokens.length - 1 : close].end], result, cjs);
    }

    return;
  }

  if(member(tokens, i)) return;

  if(t.value == 'require') requireCall(tokens, i, result, cjs);
  else if(t.value == 'exports') exportsAssign(tokens, i, i + 1, result, cjs, false);
  else if(t.value == 'module' && tokens[i + 1]?.value == '.' && tokens[i + 2]?.value == 'exports') exportsAssign(tokens, i, i + 3, result, cjs, true);
  else if(t.value == 'Object') objectCall(tokens, i, result, cjs);
}

/* a declarator whose initializer, at tokens[init], is nothing but
 * require(...) or _interopRequireWildcard(require(...)): notes its
 * bindings and, for a plain name, the name for Object.keys(name)
 */
function declarator(tokens, start, init, cjs) {
  const wrapped = tokens[init]?.value == '_interopRequireWildcard' && tokens[init + 1]?.value == '(';
  const req = wrapped ? init + 2 : init;

  if(tokens[req]?.value != 'require' || tokens[req].type != 'identifier') return;

  const call = callArguments(tokens, req + 1);

  if(!call || (wrapped && tokens[call.close + 1]?.value != ')')) return;

  const after = tokens[call.close + (wrapped ? 2 : 1)];

  /* require('x').y or require('x')(z) is more than the call */
  if(after && !(after.value == ';' || after.value == ',' || (after.nl && !/^(\.|\?\.|\(|\[)$/.test(after.value)))) return;

  const p = tokens[start];

  if(isBinding(p) && tokens[start + 1]?.value == '=') {
    cjs.stars.set(name(p), req);

    if(!wrapped) cjs.bindings.set(req, [{ imported: 'default', local: name(p) }]);
  } else if(p.value == '{' && !wrapped) {
    cjs.bindings.set(req, objectBindings(tokens, start));
  }
}

/* the bindings of the object pattern at tokens[open]: { a, b: c, d = 1 }
 * gives a, b as c, d. A nested pattern, computed key or rest gives [].
 */
function objectBindings(tokens, open) {
  const close = closer(tokens, open),
    bindings = [];

  for(let k = open + 1; k < close; ) {
    const key = tokens[k];
    let local = key;

    if(!isName(key) && key.type != 'stringLiteral') return [];

    if(tokens[k + 1]?.value == ':') {
      local = tokens[k + 2];
      k += 2;
    }

    if(!local || !isBinding(local)) return [];

    bindings.push({ imported: name(key), local: name(local) });
    k++;

    if(tokens[k]?.value == '=') while(k < close && !(tokens[k].depth == key.depth && tokens[k].value == ',')) k++;

    if(tokens[k]?.value == ',') k++;
    else if(k != close) return [];
  }

  return bindings;
}

/* the require(x) or require.resolve(x) call at tokens[i], added to
 * result.imports once: returns its index, -1 when it is no such call
 */
function requireCall(tokens, i, result, cjs) {
  if(cjs.calls.has(i)) return cjs.calls.get(i);

  const resolve = tokens[i + 1]?.value == '.' && tokens[i + 2]?.value == 'resolve';
  const call = callArguments(tokens, resolve ? i + 3 : i + 1);
  let index = -1;

  if(call && call.args.length == 1 && call.args[0].length) {
    const record = { kind: resolve ? 'require-resolve' : 'require-call', path: null, bindings: (!resolve && cjs.bindings.get(i)) || [], range: [tokens[i].start, tokens[call.close].end] };

    specifier(record, call.args[0]);
    record.line = tokens[i].line;
    index = result.imports.push(record) - 1;
  }

  cjs.calls.set(i, index);
  return index;
}

/* merve compares only the first character after exports.x, so == and ===
 * count as an assignment, as on Node
 */
const ASSIGN = /^={1,3}$/;

/* exports.x = , exports['x'] = , and with module first, module.exports =
 * followed by an object literal or require('y'). tokens[k] follows the
 * exports token. Every module.exports = drops the re-exports before it.
 */
function exportsAssign(tokens, i, k, result, cjs, module) {
  const t = tokens[k];
  let key, eq;

  if(t?.value == '.' && isName(tokens[k + 1]) && !tokens[k + 1].value.includes('\\')) {
    key = tokens[k + 1];
    eq = k + 2;
  } else if(t?.value == '[' && tokens[k + 1]?.type == 'stringLiteral' && tokens[k + 2]?.value == ']') {
    key = tokens[k + 1];
    eq = k + 3;
  }

  if(key) {
    if(ASSIGN.test(tokens[eq]?.value ?? '')) addExport(result, key, null, [tokens[i].start, tokens[eq].end]);

    return;
  }

  if(!module || !ASSIGN.test(t?.value ?? '')) return;

  result.exports = result.exports.filter(e => !(e.kind == 'commonjs' && e.name == '*'));

  if(t.value != '=') return;

  if(tokens[k + 1]?.value == '{') literal(tokens, i, k + 1, result, cjs);
  else requireReexport(tokens, k + 1, [tokens[i].start, tokens[k + 4]?.end], result, cjs);
}

/* the names of module.exports = { a, b: c, 'd': e, ...require('y') },
 * as merve reads them: a value is a name or require('y'), the first other
 * value or a getter ends the list, the names before it are kept
 */
function literal(tokens, i, open, result, cjs) {
  const close = closer(tokens, open);
  const range = [tokens[i].start, tokens[close < 0 ? tokens.length - 1 : close].end];

  for(let k = open + 1; ; ) {
    const t = tokens[k];
    let next;

    if(!t) return;

    if(isName(t) && !t.value.includes('\\')) {
      if(t.value == 'get' && isName(tokens[k + 1]) && tokens[k + 2]?.value == '(') return;

      if(tokens[k + 1]?.value == ':') {
        if((next = propertyValue(tokens, k + 2, range, result, cjs)) < 0) return;

        addExport(result, t, tokens[k + 2].type == 'identifier' ? tokens[k + 2] : null, range);
      } else {
        addExport(result, t, t, range);
        next = k + 1;
      }
    } else if(t.type == 'stringLiteral') {
      next = k + 1;

      if(tokens[k + 1]?.value == ':') {
        if((next = propertyValue(tokens, k + 2, range, result, cjs)) < 0) return;

        addExport(result, t, tokens[k + 2].type == 'identifier' ? tokens[k + 2] : null, range);
      }
    } else if(t.value == '...') {
      next = requireReexport(tokens, k + 1, range, result, cjs) + 1;

      if(!next) {
        if(!isName(tokens[k + 1])) return;

        next = k + 2;
      }
    } else {
      return;
    }

    if(tokens[next]?.value != ',') return;

    k = next + 1;
  }
}

/* a property value merve accepts at tokens[k]: require('y'), added as a
 * re-export, or a name. Returns the index after it, -1 for anything else.
 */
function propertyValue(tokens, k, range, result, cjs) {
  const end = requireReexport(tokens, k, range, result, cjs);

  if(end >= 0) return end + 1;

  return isName(tokens[k]) && !tokens[k].value.includes('\\') ? k + 1 : -1;
}

/* require('y') at tokens[k], a quoted string as the only argument: adds a
 * re-export of y. Returns the index of the ), or -1.
 */
function requireReexport(tokens, k, range, result, cjs) {
  if(tokens[k]?.value != 'require' || tokens[k].type != 'identifier') return -1;
  if(tokens[k + 1]?.value != '(' || tokens[k + 2]?.type != 'stringLiteral' || tokens[k + 3]?.value != ')') return -1;

  const importIndex = requireCall(tokens, k, result, cjs),
    imp = result.imports[importIndex];

  result.exports.push({ kind: 'commonjs', name: '*', local: null, imported: null, path: imp.path, importIndex, range, nameRange: imp.range, line: imp.line });
  return k + 3;
}

/* a commonjs export record for the name token key, local the token of the
 * local name or null. A name with a lone surrogate is skipped, as by merve.
 */
function addExport(result, key, local, range) {
  const n = name(key);

  if(!n.isWellFormed()) return;

  const record = { kind: 'commonjs', name: n, local: local ? name(local) : null, range, nameRange: [key.start, key.end] };

  if(local) record.localRange = [local.start, local.end];

  record.line = key.line;
  result.exports.push(record);
}

/* the getter of Object.defineProperty: get: function f() { or get() { */
const GETTER = ['get', [':', 'function', ['%']], '(', ')', '{'];

/* Object[.prototype].hasOwnProperty.call(o, k) */
const HASOWN = ['Object', ['.', 'prototype'], '.', 'hasOwnProperty', '.', 'call', '(', '%', ',', '=k', ')'];

/* Object.defineProperty(exports, 'x', { value: or { get() { return o.x } })
 */
const DEFINE = [
  ...['Object', '.', 'defineProperty', '(', 'exports', ',', '$', ',', '{', ['enumerable', ':', 'true', ',']],
  { alt: [['value', ':'], [...GETTER, 'return', '%', [{ alt: [['.', '%'], ['[', '"', ']']] }], ';?', '}', ',?', '}', ')']] },
];

/* the re-export loop Babel and TypeScript emit for export * from 'y':
 *
 *   Object.keys(_y).forEach(function (k) {
 *     if (k === 'default' || k === '__esModule') return;
 *     exports[k] = _y[k];
 *   });
 */
const FOREACH = [
  ...['Object', '.', 'keys', '(', '=id', ')', '.', 'forEach', '(', 'function', '(', '=k', ')', '{'],
  {
    alt: [
      [
        ...['if', '(', '=k', '===', "'default", '||', '=k', '===', "'__esModule", ')', 'return', ';?'],
        ['if', '(', ...HASOWN, ')', 'return', ';?'],
        ['if', '(', '=k', 'in', 'exports', '&&', 'exports', '[', '=k', ']', '===', '=id', '[', '=k', ']', ')', 'return', ';?'],
      ],
      ['if', '(', '=k', '!==', "'default", ['&&', '!', { alt: [HASOWN, ['%', '.', 'hasOwnProperty', '(', '=k', ')']] }], ')'],
    ],
  },
  {
    alt: [
      ['exports', '[', '=k', ']', '=', '=id', '[', '=k', ']', ';?'],
      [...['Object', '.', 'defineProperty', '(', 'exports', ',', '=k', ',', '{', 'enumerable', ':', 'true', ','], ...GETTER, ...['return', '=id', '[', '=k', ']', ';?', '}', ',?', '}', ')', ';?']],
    ],
  },
  '}',
  ')',
];

/* Object.defineProperty(exports, 'x', ...) adds x, Object.keys(_y)
 * .forEach(...) at the top level re-exports what _y was required from
 */
function objectCall(tokens, i, result, cjs) {
  let vars = {},
    end = match(tokens, i, DEFINE, vars);

  if(end >= 0) return addExport(result, vars.$, null, [tokens[i].start, tokens[end - 1].end]);

  if(tokens[i].depth != 0 || (end = match(tokens, i, FOREACH, (vars = {}))) < 0) return;

  const req = cjs.stars.get(name(vars.id)),
    importIndex = req === undefined ? -1 : cjs.calls.get(req);

  if(importIndex >= 0) {
    const { path } = result.imports[importIndex];

    result.exports.push({ kind: 'commonjs', name: '*', local: null, imported: null, path, importIndex, range: [tokens[i].start, tokens[end - 1].end], nameRange: [vars.id.start, vars.id.end], line: vars.id.line });
  }
}

/* matches a token pattern at tokens[k]: the index after it, or -1.
 *
 *   item       matches
 *   'exports'  exports or module.exports
 *   '%'        any name
 *   '=k'       a name, the same one at every =k, its token kept in vars.k
 *   '$'        any string literal, its token kept in vars.$
 *   '"'        any string literal
 *   "'abc"     the string literal 'abc' or "abc"
 *   ';?'       an optional ;
 *   [ ... ]    an optional group
 *   { alt }    the first of the alternatives that matches
 *   other      a token with that value
 */
function match(tokens, k, items, vars) {
  for(const item of items) {
    const t = tokens[k];

    if(Array.isArray(item)) {
      const j = match(tokens, k, item, vars);

      if(j >= 0) k = j;
    } else if(typeof item == 'object') {
      if((k = item.alt.reduce((j, alt) => (j >= 0 ? j : match(tokens, k, alt, vars)), -1)) < 0) return -1;
    } else if(item.length > 1 && item.endsWith('?')) {
      if(t?.value == item.slice(0, -1)) k++;
    } else if(!t) {
      return -1;
    } else if(item == 'exports') {
      if(t.value == 'exports') k++;
      else if(t.value == 'module' && tokens[k + 1]?.value == '.' && tokens[k + 2]?.value == 'exports') k += 3;
      else return -1;
    } else if(item == '%' || /^=\w/.test(item)) {
      if(!isName(t)) return -1;

      if(item != '%') {
        const v = item.slice(1);

        if(!vars[v]) vars[v] = t;
        else if(vars[v].value != t.value) return -1;
      }

      k++;
    } else if(item == '$' || item == '"' || item[0] == "'") {
      if(t.type != 'stringLiteral' || (item[0] == "'" && string(t.value) != item.slice(1))) return -1;

      if(item == '$') vars.$ = t;

      k++;
    } else if(t.value == item && t.type != 'stringLiteral') {
      k++;
    } else {
      return -1;
    }
  }

  return k;
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
