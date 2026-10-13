/* parser/cpp.js: parseDefine, Preprocessor.
 * rudimentary C preprocessor: #define (object-like, function-like,
 * variadic) and macro expansion; other directives pass through untouched.
 * rule: tokens are { kind: 'id' | 'op' | 'ws', text }; no #if, no #include.
 * limits: no rescan with the text after an expansion, no __VA_OPT__. */
import CLexer from '../lexer/c.js';

const SPACE = /(whitespace|newline|continuation|comment)$/i;
const IDENT = /^[A-Za-z_]\w*$/;
const PASTE = { kind: 'paste', text: '##' };
const BLANK = { kind: 'ws', text: ' ' };
const COMMA = { kind: 'op', text: ',' };

const word = text => ({ kind: IDENT.test(text) ? 'id' : 'op', text });
const isws = t => t.kind == 'ws';
const text = tokens => tokens.map(t => t.text).join('');

function trim(tokens) {
  let a = 0,
    b = tokens.length;

  while(a < b && isws(tokens[a])) a++;
  while(b > a && isws(tokens[b - 1])) b--;

  return tokens.slice(a, b);
}

/* source text to tokens; a run of whitespace and comments is one 'ws' */
function lex(source) {
  const out = [];

  for(const t of new CLexer(source, CLexer.LONGEST)) {
    const kind = SPACE.test(t.type) ? 'ws' : IDENT.test(t.lexeme) ? 'id' : 'op';
    /*const last = out.at(-1);

    if(kind == 'ws' && last && isws(last)) last.text += t.lexeme;
    else*/
    const tok = { kind, type: t.type, text: t.lexeme };
    out.push(tok);
  }

  return out;
}

/* parseDefine: parses one macro definition.
 *
 * ```js
 * parseDefine('#define SQ(x) ((x)*(x))');
 * // { name: 'SQ', params: ['x'], variadic: false, body: [...] }
 * parseDefine('LOG(fmt, ...) printf(fmt, __VA_ARGS__)');  // "#define " optional
 * ```
 *
 *   string  source  one directive, `\` continuations allowed
 *
 *   returns  { name, params, variadic, body }; params is null for an
 *            object-like macro; a variadic one ends with its rest name
 *   throws   SyntaxError if it is not a well-formed #define
 */
export function parseDefine(source) {
  const toks = lex(/^\s*#/.test(source) ? source : '#define ' + source);
  const fail = why => {
    throw new SyntaxError(`#define: ${why}`);
  };
  let i = 0;
  const next = () => {
    while(toks[i] && isws(toks[i])) i++;
    return toks[i];
  };

  if(toks[i++]?.type != 'pp_hash' || next()?.text != 'define') fail('not a #define');
  i++;

  const name = next();

  //for(let j = i; j < i+6; j++) console.log(`toks[${j}]?.type='${toks[j]?.type} lexeme='${toks[j]?.text}'`);

  if(name?.kind != 'id') fail('missing macro name');
  i++;

  //console.log(name?.kind);

  let params = null,
    variadic = false;

  if(toks[i]?.text == '(') {
    params = [];
    i++;

    for(let t = next(); t?.text != ')'; t = next()) {
      console.log(`kind='${t?.kind}'`);

      if(t?.kind == 'id') {
        i++;
        if(next()?.text == '...') ((variadic = true), i++);
        params.push(t.text);
      } else if(t?.text == '...') {
        variadic = true;
        params.push('__VA_ARGS__');
        i++;
      } else fail('bad parameter list');

      if(variadic && next()?.text != ')') fail('... must be last');
      if(next()?.text == ',') i++;
    }
    i++;
  }

  const body = [];
  for(; i < toks.length && toks[i].type != 'pp_newline'; i++) body.push(isws(toks[i]) ? BLANK : toks[i]);

  const def = { name: name.text, params, variadic, body: trim(body).map(t => (t.text == '##' ? PASTE : t)) };
  //console.log(`parseDefine('${source}')`, def);
  return def;
}

/* stringify: `#x`; the tokens as one string literal */
function stringify(arg) {
  const s = arg.map(t => (isws(t) ? ' ' : /^\w*["']/.test(t.text) ? t.text.replace(/[\\"]/g, '\\$&') : t.text)).join('');

  return { kind: 'op', text: `"${s}"` };
}

/* the arguments of an invocation whose `(` is toks[open]: [args, index of `)`] */
function collectArgs(toks, open) {
  const args = [[]];

  for(let depth = 0, k = open; k < toks.length; k++) {
    const t = toks[k];

    if(t.kind == 'op' && t.text == '(' && depth++ == 0) continue;
    if(t.kind == 'op' && t.text == ')' && --depth == 0) return [args.map(trim), k];
    if(depth == 1 && t.kind == 'op' && t.text == ',') args.push([]);
    else args[args.length - 1].push(t);
  }

  throw new SyntaxError('unterminated macro invocation');
}

/* Preprocessor: a table of macros and the expansion over it.
 *
 * ```js
   const pp = new Preprocessor();
   pp.define('#define ADD(a, b) ((a) + (b))');
   pp.expand('x = ADD(1, 2);');             // "x = ((1) + (2));"
   pp.process('#define N 3\nint a[N];\n');  // "\nint a[3];\n"
 * ```
 *
 *   macros   Map of name to parseDefine() result
 *   define(source)   adds one macro, returns it
 *   undef(name)      removes one macro
 *   expand(source)   source with every macro expanded
 *   process(source)  expand() that also obeys #define and #undef lines,
 *                    drops them, and leaves other directives as they are
 *
 *   throws  SyntaxError for a bad #define or an unterminated invocation
 */
export class Preprocessor {
  macros = new Map();

  define(source) {
    const m = parseDefine(source);

    this.macros.set(m.name, m);
    return m;
  }

  undef(name) {
    return this.macros.delete(name);
  }

  expand(source) {
    return text(this.#expand(lex(source), new Set()));
  }

  process(source) {
    const toks = lex(source);
    let out = '';

    for(let i = 0; i < toks.length;) {
      if(toks[i].type != 'pp_hash') {
        let j = i;
        while(j < toks.length && toks[j].type != 'pp_hash') j++;
        out += text(this.#expand(toks.slice(i, j), new Set()));
        i = j;
        continue;
      }

      let j = i;
      while(j < toks.length && toks[j].type != 'pp_newline') j++;

      const line = toks.slice(i, j + 1);
      const word = line.find((t, k) => k && !isws(t))?.text;

      out += line[0].text.slice(0, -1);
      if(word == 'define') this.define(text(line));
      else if(word == 'undef') this.undef(line.find((t, k) => k && t.kind == 'id' && t.text != 'undef')?.text);
      else out += text(line).slice(line[0].text.length - 1);
      if(word == 'define' || word == 'undef') out += line.at(-1).type == 'pp_newline' ? line.at(-1).text : '';
      i = j + 1;
    }

    return out;
  }

  #expand(toks, hide) {
    const out = [];

    for(let i = 0; i < toks.length; i++) {
      const t = toks[i];
      const m = t.kind == 'id' && !hide.has(t.text) && this.macros.get(t.text);

      if(!m) {
        out.push(t);
        continue;
      }

      const inner = new Set(hide).add(m.name);

      if(!m.params) {
        out.push(...this.#expand(this.#substitute(m, [], hide), inner));
        continue;
      }

      let j = i + 1;
      while(toks[j] && isws(toks[j])) j++;

      if(toks[j]?.text != '(') {
        out.push(t);
        continue;
      }

      const [args, end] = collectArgs(toks, j);

      i = end;
      out.push(...this.#expand(this.#substitute(m, args, hide), inner));
    }

    return out;
  }

  /* the body of `m` with parameters replaced, then `#` and `##` applied */
  #substitute(m, args, hide) {
    const { params, variadic, body } = m;
    const n = params ? params.length : 0;
    const given = args.length == 1 && !args[0].length && n == 0 ? [] : args;

    if(params && given.length > n && !variadic) throw new SyntaxError(`${m.name}: too many arguments`);

    /* the variadic parameter takes the rest, commas included */
    const actual = params ? params.map((_, k) => given[k] ?? []) : [];
    if(variadic) actual[n - 1] = given.slice(n - 1).flatMap((a, k) => (k ? [COMMA, BLANK, ...a] : a));

    const pieces = [];
    const nonws = k => {
      while(body[k] && isws(body[k])) k += k < 0 ? 0 : 1;
      return body[k];
    };

    for(let k = 0; k < body.length; k++) {
      const t = body[k];
      const p = t.kind == 'id' && params ? params.indexOf(t.text) : -1;
      const prev = pieces.findLast(x => !isws(x));
      let b = k + 1;
      while(body[b] && isws(body[b])) b++;

      if(params && t.text == '#' && body[b]?.kind == 'id' && params.includes(body[b].text)) {
        pieces.push(stringify(actual[params.indexOf(body[b].text)]));
        k = b;
      } else if(p < 0) pieces.push(t);
      else if(prev === PASTE || body[b] === PASTE) {
        const raw = actual[p];

        /* `, ## __VA_ARGS__` with no variadic arguments drops the comma */
        if(variadic && p == n - 1 && !raw.length && prev === PASTE) {
          while(pieces.pop() !== PASTE);
          while(pieces.length && isws(pieces.at(-1))) pieces.pop();
          if(pieces.at(-1)?.text == ',') pieces.pop();
        } else pieces.push(...(raw.length ? raw : [{ kind: 'pm', text: '' }]));
      } else pieces.push(...this.#expand(actual[p], hide));
    }

    /* apply ##: glue the token before to the token after */
    const out = [];
    let glue = false;

    for(const t of pieces) {
      if(t === PASTE) {
        while(out.length && isws(out.at(-1))) out.pop();
        glue = true;
      } else if(glue && isws(t)) continue;
      else if(glue && out.length) {
        out.push(word(out.pop().text + t.text));
        glue = false;
      } else {
        out.push(t);
        glue = false;
      }
    }

    return out;
  }
}

export default Preprocessor;
