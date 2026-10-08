/* parser/lex.js: parseLex(), the AST of a lex/flex .l file and what it turns into.
 * depends on: lexer/lex.js (tokens), the `lexer` module (createLexer/scan).
 * rule: LexFile.toString() gives back the exact text that was parsed; every
 * piece keeps the whitespace before it (`lead`), so edits stay local. */
import { Lexer } from 'lexer';
import LexLexer from '../lexer/lex.js';

/* regular expressions: one class per flex construct; toString() is flex
 * source again, toJS() the same pattern for the `lexer` module */

const PosixClasses = {
  alnum: 'a-zA-Z0-9',
  alpha: 'a-zA-Z',
  blank: ' \\t',
  cntrl: '\\x00-\\x1f\\x7f',
  digit: '0-9',
  graph: '!-~',
  lower: 'a-z',
  print: ' -~',
  punct: '!-\\/:-@\\[-`{-~',
  space: ' \\t\\n\\r\\f\\v',
  upper: 'A-Z',
  xdigit: '0-9A-Fa-f',
};

const SimpleEscapes = { a: '\\x07', b: '\\x08', f: '\\f', n: '\\n', r: '\\r', t: '\\t', v: '\\v' };

const hex2 = n => '\\x' + (n & 255).toString(16).padStart(2, '0');

/* flex escape `\n`, `\x41`, `\101`, `\.` as it is written in a JS regexp */
function escapeToJS(raw) {
  const c = raw[1];

  if(/^\\(?:u[0-9A-Fa-f]{4}|[sSdDwW])$/.test(raw)) return raw;
  if(c == 'x') return hex2(parseInt(raw.slice(2), 16));
  if(/[0-7]/.test(c)) return hex2(parseInt(raw.slice(1), 8));
  if(c in SimpleEscapes) return SimpleEscapes[c];
  if(/\w/.test(c)) return c;

  return '\\' + c;
}

/* a plain character as a JS regexp source */
const charToJS = c => (/[\\^$.*+?()[\]{}|\/-]/.test(c) ? '\\' + c : c);

/* text inside a flex "string", with its escapes, as a JS regexp source */
function stringToJS(body) {
  let out = '';

  for(let i = 0; i < body.length; i++) {
    if(body[i] == '\\') {
      const m = /^\\(?:u[0-9A-Fa-f]{4}|x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|[\s\S])/.exec(body.slice(i));
      out += escapeToJS(m[0]);
      i += m[0].length - 1;
    } else out += charToJS(body[i]);
  }

  return out;
}

export class RegexNode {
  get type() {
    return this.constructor.name;
  }
}

/* `a|b` */
export class Alt extends RegexNode {
  constructor(items) {
    super();
    this.items = items;
  }
  toString() {
    return this.items.join('|');
  }
  toJS() {
    return this.items.map(i => i.toJS()).join('|');
  }
}

/* `ab`; a `/` inside is trailing context: `foo/bar` */
export class Seq extends RegexNode {
  constructor(items) {
    super();
    this.items = items;
  }
  toString() {
    return this.items.join('');
  }
  toJS() {
    const i = this.items.findIndex(n => n instanceof Slash);
    const js = a => a.map(n => n.toJS()).join('');

    return i < 0 ? js(this.items) : `${js(this.items.slice(0, i))}(?=${js(this.items.slice(i + 1))})`;
  }
}

export class Slash extends RegexNode {
  toString() {
    return '/';
  }
  toJS() {
    return '/';
  }
}

/* `"text"`: raw keeps the quotes and escapes */
export class Literal extends RegexNode {
  constructor(raw) {
    super();
    this.raw = raw;
  }
  get text() {
    return this.raw.slice(1, -1).replace(/\\(x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|[\s\S])/g, (m, e) => {
      if(e[0] == 'x') return String.fromCharCode(parseInt(e.slice(1), 16));
      if(/[0-7]/.test(e[0])) return String.fromCharCode(parseInt(e, 8));
      return { a: '\x07', b: '\b', f: '\f', n: '\n', r: '\r', t: '\t', v: '\v' }[e] ?? e;
    });
  }
  toString() {
    return this.raw;
  }
  toJS() {
    return `(?:${stringToJS(this.raw.slice(1, -1))})`;
  }
}

/* one character: `a`, `\n`, `\x41`, `\.` */
export class Char extends RegexNode {
  constructor(raw) {
    super();
    this.raw = raw;
  }
  toString() {
    return this.raw;
  }
  toJS() {
    return this.raw[0] == '\\' ? escapeToJS(this.raw) : charToJS(this.raw);
  }
}

/* `[a-z]`, `[^"\n]`, `[[:alpha:]_]` */
export class CharClass extends RegexNode {
  constructor(raw) {
    super();
    this.raw = raw;
  }
  get negated() {
    return this.raw[1] == '^';
  }
  toString() {
    return this.raw;
  }
  toJS() {
    const neg = this.negated;
    const body = this.raw.slice(neg ? 2 : 1, -1);
    let out = '';

    for(let i = 0; i < body.length; i++) {
      const c = body[i];
      let m;

      if(c == '[' && (m = /^\[:(\w+):\]/.exec(body.slice(i))) && m[1] in PosixClasses) {
        out += PosixClasses[m[1]];
        i += m[0].length - 1;
      } else if(c == '\\') {
        m = /^\\(?:u[0-9A-Fa-f]{4}|x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|[\s\S])/.exec(body.slice(i));
        out += escapeToJS(m[0]);
        i += m[0].length - 1;
      } else if(c == '[' || c == ']' || c == '/') out += '\\' + c;
      else out += c;
    }

    return `[${neg ? '^' : ''}${out}]`;
  }
}

/* `.`: any character but a newline */
export class Any extends RegexNode {
  toString() {
    return '.';
  }
  toJS() {
    return '[^\\n]';
  }
}

/* `{NAME}`: a definition from section 1 */
export class Ref extends RegexNode {
  constructor(raw) {
    super();
    this.raw = raw;
  }
  get name() {
    return this.raw.slice(1, -1);
  }
  toString() {
    return this.raw;
  }
  toJS() {
    return '{' + defineName(this.name) + '}';
  }
}

/* `(...)`, or `(?:...)`, `(?i:...)` with its flags */
export class Group extends RegexNode {
  constructor(child, open = '(') {
    super();
    this.child = child;
    this.open = open;
  }
  toString() {
    return `${this.open}${this.child})`;
  }
  toJS() {
    return `(?:${this.child.toJS()})`;
  }
}

/* `a*`, `a+`, `a?`, `a{2,4}`: quantifier is the source text */
export class Repeat extends RegexNode {
  constructor(child, quantifier) {
    super();
    this.child = child;
    this.quantifier = quantifier;
  }
  toString() {
    return `${this.child}${this.quantifier}`;
  }
  toJS() {
    const inner = this.child.toJS();
    const atomic = this.child instanceof Group || this.child instanceof CharClass || this.child instanceof Any || this.child instanceof Char || this.child instanceof Ref;

    return (atomic ? inner : `(?:${inner})`) + this.quantifier;
  }
}

/* `^`: beginning of a line */
export class Bol extends RegexNode {
  toString() {
    return '^';
  }
  toJS() {
    return '(?<![^\\n])';
  }
}

/* `$`: end of a line */
export class Eol extends RegexNode {
  toString() {
    return '$';
  }
  toJS() {
    return '(?![^\\n])';
  }
}

/* `<<EOF>>` */
export class Eof extends RegexNode {
  toString() {
    return '<<EOF>>';
  }
  toJS() {
    return '';
  }
}

/* names of definitions become `lexer` defines: word characters only */
const defineName = name => name.replace(/\W/g, '_');

/* flex pattern text -> RegexNode tree */
export function parseRegex(src, jison = false) {
  let pos = 0;

  const fail = msg => {
    throw new SyntaxError(`${msg} at ${pos} in pattern ${src}`);
  };

  function alt() {
    const items = [seq()];

    while(src[pos] == '|') {
      pos++;
      items.push(seq());
    }

    return items.length > 1 ? new Alt(items) : items[0];
  }

  function seq() {
    const items = [];

    while(pos < src.length && src[pos] != '|' && src[pos] != ')') {
      if(src[pos] == '/') {
        pos++;
        items.push(new Slash());
      } else items.push(postfix(items.length == 0));
    }

    return items.length == 1 ? items[0] : new Seq(items);
  }

  function postfix(first) {
    let node = atom(first);

    for(let m; (m = /^(?:[*+?]|\{\d+(?:,\d*)?\})/.exec(src.slice(pos))); ) {
      pos += m[0].length;
      node = new Repeat(node, m[0]);
    }

    return node;
  }

  function atom(first) {
    const c = src[pos];
    let m;

    if(c == '(') {
      m = /^\(\?[-a-z]*:|^\(/.exec(src.slice(pos));
      pos += m[0].length;
      const child = alt();
      if(src[pos] != ')') fail('missing )');
      pos++;
      return new Group(child, m[0]);
    }

    if(c == '"' || (c == "'" && jison)) {
      m = (c == '"' ? /^"(?:\\.|[^"\\])*"/ : /^'(?:\\.|[^'\\])*'/).exec(src.slice(pos)) ?? fail('unterminated string');
      pos += m[0].length;
      return new Literal(m[0]);
    }

    if(c == '[') {
      m = /^\[\^?\]?(?:\[:\^?\w+:\]|\\.|[^\]\\])*\]/.exec(src.slice(pos)) ?? fail('unterminated class');
      pos += m[0].length;
      return new CharClass(m[0]);
    }

    if(c == '{') {
      m = /^\{[^{}]*\}/.exec(src.slice(pos)) ?? fail('unterminated {');
      pos += m[0].length;
      return new Ref(m[0]);
    }

    if(c == '\\') {
      m = /^\\(?:u[0-9A-Fa-f]{4}|x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|[\s\S])/.exec(src.slice(pos));
      pos += m[0].length;
      return new Char(m[0]);
    }

    pos++;
    if(c == '.') return new Any();
    if(c == '^' && first) return new Bol();
    if(c == '$' && (pos == src.length || src[pos] == '|' || src[pos] == ')' || src[pos] == '/')) return new Eol();

    return new Char(c);
  }

  const tree = alt();
  if(pos < src.length) fail('unexpected ' + src[pos]);
  return tree;
}

/* the file: items of the definitions and the rules section */

/* `%{ ... %}`, `%top{ ... }`, an indented line or a comment, copied as it is */
export class Code {
  constructor(kind, text, lead = '\n') {
    this.kind = kind; // 'block' | 'top' | 'indented' | 'comment'
    this.text = text;
    this.lead = lead;
  }
  get type() {
    return 'Code';
  }
  toString() {
    return this.lead + this.text;
  }
}

/* `%option a b=c`, `%x name`, `%s name`: args are {text, lead}, `=` kept as its own arg */
export class Directive {
  constructor(name, args = [], lead = '\n') {
    this.name = name;
    this.args = args;
    this.lead = lead;
  }
  get type() {
    return 'Directive';
  }
  /* names of a %x / %s line, or the option words of %option */
  get words() {
    return this.args.map(a => a.text).filter(t => t != '=');
  }
  get exclusive() {
    return this.name == '%x' || this.name == '%xs';
  }
  toString() {
    return this.lead + this.name + this.args.map(a => a.lead + a.text).join('');
  }
}

/* `NAME  pattern` in section 1 */
export class Definition {
  constructor(name, pattern, lead = '\n', sep = '\t') {
    this.name = name;
    this.pattern = pattern; // RegexNode, or null when the value does not parse
    this.lead = lead;
    this.sep = sep;
    this.raw = String(pattern);
  }
  get type() {
    return 'Definition';
  }
  toString() {
    return this.lead + this.name + this.sep + this.pattern;
  }
}

/* `<STATE>pattern  { action }`; conditions is null, or the names in `<a,b>` */
export class Rule {
  constructor(conditions, pattern, action, lead = '\n', sep = '\t') {
    this.conditions = conditions;
    this.pattern = pattern; // RegexNode, or Eof
    this.action = action; // text of the action, '|' for fall-through
    this.lead = lead;
    this.sep = sep;
  }
  get type() {
    return 'Rule';
  }
  toString() {
    const conds = this.conditions ? `<${this.conditions.join(',')}>` : '';
    return this.lead + conds + this.pattern + (this.action == null ? '' : this.sep + this.action);
  }
}

/* `<STATE>{` opens a block whose rules all get that start condition; `}` closes it */
export class Scope {
  constructor(conditions, lead = '\n') {
    this.conditions = conditions;
    this.lead = lead;
  }
  get type() {
    return 'Scope';
  }
  toString() {
    return `${this.lead}<${this.conditions.join(',')}>{`;
  }
}

export class ScopeEnd {
  constructor(lead = '\n') {
    this.lead = lead;
  }
  get type() {
    return 'ScopeEnd';
  }
  toString() {
    return this.lead + '}';
  }
}

/* what a rule's action means to the `lexer` module: only `return NAME;`, `return 'x';`, BEGIN(), yy_push_state() and yy_pop_state() are understood, anything else
 * is C code that cannot run here */
export function actionInfo(action) {
  const a = (action ?? '').replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');
  const top = topLevel(a);
  const ret = /\breturn\b\s*\(?\s*(?:([A-Za-z_]\w*)|(['"])((?:\\.|(?!\2)[^\\])+)\2)\s*\)?\s*(?=;|\}|$)/m.exec(top);
  const begin = /\bBEGIN\s*\(\s*(\w+)\s*\)|\bthis\.begin\(\s*["'](\w+)["']\s*\)/.exec(a);
  const push = /\byy_push_state\s*\(\s*(\w+)\s*\)|\bthis\.pushState\(\s*["'](\w+)["']\s*\)/.exec(a);
  const pop = /\byy_pop_state\s*\(|\bthis\.popState\s*\(/.test(a);

  return { token: ret?.[1] ?? ret?.[3], returns: /\breturn\b/.test(top), begin: begin?.[1] ?? begin?.[2], push: push?.[1] ?? push?.[2], pop };
}

/* the action without its `{ }` / `%{ %}` wrapper and without nested blocks, so a `return`
 * inside an `if { }` does not count as the rule's return */
function topLevel(a) {
  const body = a.trim().replace(/^(?:%\{|\{)([\s\S]*?)(?:%\}|\})$/, '$1');
  let depth = 0,
    out = '';

  for(const c of body) {
    if(c == '{') depth++;
    else if(c == '}') depth--;
    else if(!depth) out += c;
  }

  return out;
}

const sanitize = s => s.replace(/\W+/g, '_').replace(/^_+|_+$/g, '');

export class LexFile {
  constructor() {
    this.definitions = [];
    this.rules = [];
    this.user = null; // text after the second %%
    this.lead1 = '\n'; // whitespace before the first %%
    this.lead2 = '\n'; // whitespace before the second %%
    this.trail = ''; // whitespace after the last rule, when there is no user section
  }

  get type() {
    return 'LexFile';
  }

  /* the .l text again: what was parsed, byte for byte */
  toString() {
    return this.definitions.join('') + this.lead1 + '%%' + this.rules.join('') + (this.user == null ? this.trail : this.lead2 + '%%' + this.user);
  }

  /* start conditions: [[name, exclusive], ...] from the %x and %s lines */
  get states() {
    const out = [];

    for(const d of this.definitions) if(d instanceof Directive && /^%(x|s|xs|sx)$/.test(d.name)) for(const w of d.words) out.push([w, d.exclusive]);

    return out;
  }

  /* the file as plain data for the `lexer` module.
   *
   *   returns {defines, states, rules}
   *     defines  [[name, source]]  one per Definition, in file order
   *     states   [name]            INITIAL first, then the declared ones
   *     rules    [{name, token, states, source, skip, begin, push, pop, eof, action}]
   *              source is the pattern as the module reads it, without `<STATE>`
   */
  compile() {
    const defines = this.definitions.filter(d => d instanceof Definition && d.pattern).map(d => [defineName(d.name), `(?:${d.pattern.toJS()})`]);
    const declared = this.states;
    const states = ['INITIAL', ...declared.map(([n]) => n)];
    const inclusive = ['INITIAL', ...declared.filter(([, x]) => !x).map(([n]) => n)];
    const used = new Set(),
      rules = [];
    let pending = null,
      scope = null;

    const unique = base => {
      let name = base,
        n = 1;
      while(used.has(name)) name = base + '_' + ++n;
      used.add(name);
      return name;
    };

    for(const [i, r] of this.rules.entries()) {
      if(r instanceof Scope) scope = r.conditions;
      if(r instanceof ScopeEnd) scope = null;
      if(!(r instanceof Rule)) continue;

      if(r.action == '|') {
        pending = pending ?? [];
        pending.push(r);
        continue;
      }

      for(const rule of [...(pending ?? []), r]) {
        const info = actionInfo(r.action);
        const eof = rule.pattern instanceof Eof;
        const lit = rule.pattern instanceof Literal ? sanitize(rule.pattern.text) : '';
                const own = rule.conditions ?? scope;
        const conds = own ? (own.includes('*') ? states : own) : inclusive;

        rules.push({
          name: unique(info.token || lit || 'rule_' + i),
          token: info.token ?? null,
          states: conds,
          source: rule.pattern.toJS(),
          skip: !info.returns,
          begin: info.begin,
          push: info.push,
          pop: info.pop,
          eof,
          action: r.action,
        });
      }

      pending = null;
    }

    return { defines, states, rules };
  }

  /* a Lexer scanning `input` with this file's rules; the actions that
   * cannot run are dropped, so a rule without `return` skips its match.
   *
   *   string  input     source text to scan
   *   string  fileName  for error messages
   *   number  mode      Lexer.LONGEST (flex' rule) by default, Lexer.FIRST (jison's)
   *
   *   returns Lexer
   */
  createLexer(input, fileName, mode = Lexer.LONGEST) {
    const lexer = new Lexer(input, mode, fileName);
    const { defines, rules } = this.compile();

    /* {NAME} is replaced here: the module's own expansion drops long nested definitions */
    const table = new Map(defines),
      done = new Map();
    const expand = (src, depth = 0) =>
      depth > 20 ? src : src.replace(/(?<!\\)\{([A-Za-z_]\w*)\}/g, (m, name) => (table.has(name) ? (done.get(name) ?? done.set(name, expand(table.get(name), depth + 1)).get(name)) : m));

    for(const r of rules) {
      if(r.eof) continue;

      const prefix = `<${r.states.join(',')}>`;
      const { skip, begin, push, pop } = r;
      const fn =
        skip || begin || push || pop
          ? (lx, doSkip) => {
              if(begin) lx.state = begin;
              if(push) lx.pushState(push);
              if(pop) lx.popState();
              if(skip) doSkip();
            }
          : undefined;

      lexer.addRule(r.name, new RegExp(prefix + expand(r.source)), fn);
    }

    return lexer;
  }

  /* the tokens of `input`, as a generator: `for(const tok of file.scan(src))`.
   *
   *   returns Generator<Token>  tok.type is the rule name, tok.lexeme the text
   */
  *scan(input, fileName, mode) {
    const lexer = this.createLexer(input, fileName, mode);

    for(let tok; (tok = lexer.nextToken()); ) yield tok;
  }

  /* a JS module with a Lexer subclass for these rules, in the style of lib/lexer/*.js.
   *
   *   string  className  name of the exported class, e.g. 'ShellLexer'
   *
   *   returns string  module source
   */
  toLexerSource(className = 'GeneratedLexer') {
    const { defines, rules } = this.compile();
    const lit = s => `/${s}/`;
    const lines = [
      `import { define } from 'util';`,
      `import { Lexer } from 'lexer';`,
      '',
      `export class ${className} extends Lexer {`,
      `  constructor(input, mode = Lexer.LONGEST, filename, mask) {`,
      `    super(input, mode, filename, mask);`,
      '',
      `    this.addDefines();`,
      `    this.addRules();`,
      `  }`,
      '',
      `  addDefines() {`,
      ...defines.map(([n, s]) => `    this.define(${JSON.stringify(n)}, ${lit(s)});`),
      `  }`,
      '',
      `  addRules() {`,
    ];

    for(const r of rules) {
      if(r.eof) {
        lines.push(`    /* <<EOF>> rule dropped: ${r.name} */`);
        continue;
      }

      const body = [];
      if(r.begin) body.push(`lexer.state = ${JSON.stringify(r.begin)};`);
      if(r.push) body.push(`lexer.pushState(${JSON.stringify(r.push)});`);
      if(r.pop) body.push(`lexer.popState();`);
      if(r.skip) body.push(`skip();`);

      const fn = body.length ? `, (lexer, skip) => { ${body.join(' ')} }` : '';

      lines.push(`    this.addRule(${JSON.stringify(r.name)}, ${lit(`<${r.states.join(',')}>` + r.source)}${fn});`);
    }

    lines.push(`  }`, `}`, '', `globalThis.${className} = ${className};`, '', `define(${className}.prototype, { [Symbol.toStringTag]: '${className}' });`, '', `export default ${className};`, '');

    return lines.join('\n');
  }
}

/* .l text -> LexFile.
 *
 *   string  text      the contents of a .l file
 *   string  fileName  for error messages
 *
 *   returns LexFile
 *   throws  SyntaxError  on a pattern that does not parse
 */
export function parseLex(text, fileName) {
  try {
    return parseSource(text, fileName, false);
  } catch(error) {
    /* a jison file quotes patterns with '...': read it again that way */
    try {
      return parseSource(text, fileName, true);
    } catch(_) {
      throw error;
    }
  }
}

function parseSource(text, fileName, jison) {
  const file = new LexFile();
  const lexer = new LexLexer(text, Lexer.FIRST, fileName, undefined, jison);
  let pos = 0,
    section = 1,
    tok = lexer.nextToken();

  /* whitespace between the previous token and this one */
  const lead = t => {
    return text.slice(pos, t.loc.charOffset);
  };

  const take = () => {
    const t = tok;
    pos = t.loc.charOffset + t.charLength;
    tok = lexer.nextToken();
    return t;
  };

  const wrap = (pattern, fallback) => {
    try {
      return parseRegex(pattern, jison);
    } catch(e) {
      if(fallback) return null;
      throw e;
    }
  };

  while(tok) {
    const t = tok,
      l = lead(t);

    if(section == 1) {
      switch (t.type) {
        case 'codeblock':
        case 'top':
        case 'comment':
        case 'indented':
          take();
          file.definitions.push(new Code(t.type == 'codeblock' ? 'block' : t.type, t.lexeme, l));
          break;
        case 'directive': {
          take();
          const args = [];

          while(tok && tok.type.startsWith('d_')) {
            const a = tok,
              al = lead(a);
            take();
            args.push({ text: a.lexeme, lead: al });
          }

          file.definitions.push(new Directive(t.lexeme, args, l));
          break;
        }
        case 'def_name': {
          take();
          const v = tok;
          const sep = lead(v);
          take();
          file.definitions.push(new Definition(t.lexeme, wrap(v.lexeme), l, sep));
          break;
        }
        case 'section':
          take();
          section = 2;
          file.lead1 = l;
          break;
        default:
          throw new SyntaxError(`${fileName ?? ''}:${t.loc}: unexpected ${t.type} '${t.lexeme}' in the definitions section`);
      }
    } else if(section == 2) {
      switch (t.type) {
        case 'codeblock':
        case 'rule_comment':
        case 'indented':
          take();
          file.rules.push(new Code(t.type == 'codeblock' ? 'block' : t.type == 'rule_comment' ? 'comment' : t.type, t.lexeme, l));
          break;
        case 'scope':
          take();
          file.rules.push(new Scope(t.lexeme.slice(1, -2).split(','), l));
          break;
        case 'scope_end':
          take();
          file.rules.push(new ScopeEnd(l));
          break;
        case 'user_section':
          take();
          section = 3;
          file.lead2 = l;
          file.user = '';
          break;
        case 'condition':
        case 'regex':
        case 'eof': {
          let cond = null,
            first = take(),
            ruleLead = l;

          if(first.type == 'condition') {
            cond = first.lexeme.slice(1, -1).split(',');
            first = tok;
            take();
          }

          let pattern;
          if(first.type == 'eof') {
            const m = /^(?:<([\w,*]+)>)?<<EOF>>$/.exec(first.lexeme);
            if(m?.[1]) cond = m[1].split(',');
            pattern = new Eof();
          } else pattern = wrap(first.lexeme);

          let action = null,
            sep = '';

          if(tok && /^action/.test(tok.type)) {
            sep = lead(tok);
            action = tok.lexeme;
            take();

            /* a comment after the action: kept as part of it */
            while(tok && tok.type == 'action_line') {
              action += lead(tok) + tok.lexeme;
              take();
            }
          }

          file.rules.push(new Rule(cond, pattern, action, ruleLead, sep));
          break;
        }
        default:
          throw new SyntaxError(`${fileName ?? ''}:${t.loc}: unexpected ${t.type} '${t.lexeme}' in the rules section`);
      }
    } else {
      take();
      file.user = l + t.lexeme;
    }
  }

  if(section < 3) file.trail = text.slice(pos);

  return file;
}

export default parseLex;
