/**
 * A BNF/EBNF (and simple YACC-style) grammar compiler.
 *
 * Unlike a typical grammar-file parser, this one does not build an AST of
 * the .ebnf/.bnf/.y *source file* it reads. Instead, every rule it parses
 * is directly compiled into a `Rule` object from `../parser.js`
 * (Sequence/Alternative/Optional/OneOrMore/ZeroOrMore/Literal/CharClass/...)
 * - the *output* of parsing a grammar file is itself a working,
 * boost::spirit-style backtracking parser for the language that grammar
 * describes. Running *that* parser against real input (via `Capture`, see
 * ../parser.js) is what produces an AST - of the target language, not of
 * the grammar file.
 *
 * Three closely related dialects are accepted, since real-world grammar
 * files mix conventions freely:
 *   - `::=`/`:=`/`:` as the rule-definition operator (EBNF commonly uses
 *     `::=`, YACC uses a bare `:`).
 *   - `|` alternation, `(...)` grouping, `?`/`*`/`+` postfix quantifiers
 *     (optional / zero-or-more / one-or-more).
 *   - Rules terminated by `;` (YACC-style) *or* left unterminated, in which
 *     case the next `identifier` immediately followed by a definition
 *     operator is taken as the start of the next rule (plain EBNF/BNF
 *     style - there's no other way to tell where one rule's alternation
 *     ends and the next rule begins).
 *   - String literals in either `'...'` or `"..."` quotes (no escape
 *     processing - deliberately: these grammar files use backslash as a
 *     literal grammar character, e.g. `"\" EscapeSequence`, not as an
 *     escape introducer within the .ebnf file's own string syntax).
 *   - Character classes `[...]` (optionally negated with a leading `^`),
 *     and bare `#xHHHH` / `#xHHHH-#xHHHH` Unicode code point literals and
 *     ranges, both inside a character class and on their own as a Primary
 *     (e.g. `SourceCharacter ::= #x0000-#x10FFFF`).
 *   - `%token NAME...`, `%start NAME`, and other `%...` YACC declaration
 *     lines are skipped like comments (with `%token`/`%start` specifically
 *     recorded on the resulting Grammar), so a YACC grammar's declarations
 *     section and `%%` separator don't need special handling beyond that.
 *
 * Left recursion: `Sequence`/`Alternative`/etc. are all top-down,
 * PEG-style backtracking combinators (like boost::spirit::qi itself), which
 * cannot follow a directly left-recursive rule (`X ::= X a | b`) without
 * infinite-looping - a real concern here, since YACC grammars are written
 * for bottom-up LALR parsing and commonly use left recursion for what EBNF
 * would spell `b a*`. Every rule's alternatives are therefore checked for
 * immediate left recursion (an alternative whose first symbol is a
 * self-reference) and, if found, mechanically rewritten into that
 * iterative form before a Rule is built - see `compileRule()` below.
 */
import { Rule, Sequence, Alternative, Optional, OneOrMore, ZeroOrMore, Literal, CharClass, _lit } from '../parser.js';

/** Lazily resolves a named rule reference against `grammar.rules` at match
 * time (not at grammar-compile time), so forward references and recursive/
 * mutually-recursive rules work regardless of definition order. */
export class RuleRef extends Rule {
  constructor(name, grammar) {
    super();
    this.name = name;

    Object.defineProperty(this, 'grammar', { value: grammar });
  }

  match(lexer) {
    const target = this.grammar.rules[this.name];

    if(!target) throw new Error(`Undefined grammar rule: ${JSON.stringify(this.name)}`);

    return target.match(lexer);
  }
}

/** The result of compiling a grammar file: every defined rule, by name,
 * plus enough bookkeeping (`tokens`, `start`) to reflect a YACC-style
 * preamble when one was present. */
export class Grammar {
  constructor() {
    this.rules = Object.create(null);
    this.order = [];
    this.tokens = new Set();
    this.start = null;
    this.startExplicit = false;
  }

  ref(name) {
    return new RuleRef(name, this);
  }

  define(name, rule) {
    if(!(name in this.rules)) this.order.push(name);

    this.rules[name] = rule;

    if(this.start === null && !this.startExplicit) this.start = name;

    return rule;
  }

  setStart(name) {
    this.start = name;
    this.startExplicit = true;
  }

  get(name) {
    return this.rules[name];
  }

  get startRule() {
    return this.rules[this.start];
  }
}

function toSequence(items) {
  if(items.length === 0) return new Sequence();

  if(items.length === 1) return items[0];

  return new Sequence(...items);
}

function toAlternative(sequences) {
  if(sequences.length === 1) return toSequence(sequences[0]);

  return new Alternative(...sequences.map(toSequence));
}

/* Detects and eliminates immediate left recursion: a rule
 *   X ::= X a1 a2 ... | X b1 ... | c1 c2 ... | d1 ...
 * (any mix of alternatives that start with a reference to X itself, and
 * alternatives that don't) becomes
 *   X ::= (c1 c2 ... | d1 ...) (a1 a2 ... | b1 ...)*
 * i.e. one of the non-recursive ("base") alternatives, followed by zero or
 * more repetitions of one of the recursive alternatives with its leading
 * self-reference stripped off. This is the standard mechanical rewrite for
 * turning a left-recursive production into a form top-down/PEG combinators
 * (this library, and boost::spirit::qi itself) can actually run - real
 * YACC/Bison grammars (written for bottom-up LALR parsing, where left
 * recursion is normal and even preferred) rely on it routinely. Rules with
 * no self-referencing alternative at all are unaffected. */
function compileRule(name, alternatives) {
  const recursive = [],
    base = [];

  for(const seq of alternatives) {
    if(seq.length > 0 && seq[0] instanceof RuleRef && seq[0].name === name) recursive.push(seq.slice(1));
    else base.push(seq);
  }

  if(recursive.length === 0) return toAlternative(base);

  const baseRule = toAlternative(base.length ? base : [[]]);
  const tailRule = toAlternative(recursive);

  return new Sequence(baseRule, new ZeroOrMore(tailRule));
}

function isIdentStart(c) {
  return c !== undefined && /[A-Za-z_]/.test(c);
}

function isIdentPart(c) {
  return c !== undefined && /[A-Za-z0-9_-]/.test(c);
}

function isHexDigit(c) {
  return c !== undefined && /[0-9A-Fa-f]/.test(c);
}

/* Parses the raw text between `[` and `]` (already extracted, `^`
 * included if present) into a single character-testing predicate. Items may
 * be separated by whitespace (needed for the `[^ #x0012 #x0015 ...]`-style
 * classes some of these grammars use, mixing literal characters and code
 * point references space-separated inside one class) or run together
 * (`[0-9A-Za-z]`, `[-0-9A-Za-z]`). Recognizes, per item: a `#xHHHH-#xHHHH`
 * code point range, a bare `#xHHHH` code point, an `x-y` character range,
 * or (falling back for anything else, including content this doesn't
 * recognize at all - grammar files transcribed by hand are not always
 * strictly correct) a single literal character. Never throws: worst case,
 * unrecognized content just becomes a set of literal-character/no-op tests
 * instead of a meaningful class, which is preferable to failing to compile
 * the whole grammar over one malformed character class. */
function parseCharClassBody(raw) {
  let i = 0;
  let negate = false;

  if(raw[0] === '^') {
    negate = true;
    i = 1;
  }

  const tests = [];

  while(i < raw.length) {
    if(/\s/.test(raw[i])) {
      i++;
      continue;
    }

    let m = /^#x([0-9A-Fa-f]+)-#x([0-9A-Fa-f]+)/.exec(raw.slice(i));

    if(m) {
      const lo = parseInt(m[1], 16),
        hi = parseInt(m[2], 16);

      tests.push(c => {
        const cp = c.codePointAt(0);
        return cp >= lo && cp <= hi;
      });
      i += m[0].length;
      continue;
    }

    m = /^#x([0-9A-Fa-f]+)/.exec(raw.slice(i));

    if(m) {
      const cp = parseInt(m[1], 16);

      tests.push(c => c.codePointAt(0) === cp);
      i += m[0].length;
      continue;
    }

    if(i + 2 < raw.length && raw[i + 1] === '-' && raw[i + 2] !== undefined) {
      const lo = raw[i],
        hi = raw[i + 2];

      tests.push(c => c >= lo && c <= hi);
      i += 3;
      continue;
    }

    const ch = raw[i];

    tests.push(c => c === ch);
    i++;
  }

  const test = c => tests.some(fn => fn(c));

  return negate ? c => !test(c) : test;
}

function parseGrammarSource(text) {
  const grammar = new Grammar();
  const len = text.length;
  let pos = 0;

  function skipWs() {
    for(;;) {
      while(pos < len && /\s/.test(text[pos])) pos++;

      if(text.startsWith('/*', pos)) {
        const end = text.indexOf('*/', pos + 2);
        pos = end === -1 ? len : end + 2;
        continue;
      }

      if(text[pos] === '%') {
        /*if(text[pos+1]=='%') {
          pos = len;
          continue;
        }*/

        if(text[pos + 1] == '{') {
          let end = text.indexOf('%}', pos);
          pos = end == -1 ? len : end + 2;
          continue;
        }

        const eol = text.indexOf('\n', pos);
        const line = text.slice(pos, eol === -1 ? len : eol);
        const tokenMatch = /^%token\s+(.*)$/.exec(line);
        const startMatch = /^%start\s+(\S+)/.exec(line);

        if(tokenMatch) for(const name of tokenMatch[1].trim().split(/\s+/)) if (name) grammar.tokens.add(name);

        if(startMatch && !grammar.startExplicit) grammar.setStart(startMatch[1]);

        pos = eol === -1 ? len : eol + 1;
        continue;
      }

      break;
    }
  }

  function atEnd() {
    skipWs();
    return pos >= len;
  }

  function peekIdent() {
    if(!isIdentStart(text[pos])) return null;

    let end = pos + 1;

    while(isIdentPart(text[end])) end++;

    return text.slice(pos, end);
  }

  function readIdent() {
    const name = peekIdent();

    if(name === null) throw new Error(`Expected identifier at offset ${pos}: ${JSON.stringify(text.slice(pos, pos + 30))}`);

    pos += name.length;
    return name;
  }

  function readAssign() {
    skipWs();

    if(text.startsWith('::=', pos)) {
      pos += 3;
      return;
    }

    if(text.startsWith(':=', pos)) {
      pos += 2;
      return;
    }

    if(text[pos] === ':') {
      pos += 1;
      return;
    }

    throw new Error(`Expected '::=', ':=', or ':' at offset ${pos}: ${JSON.stringify(text.slice(pos, pos + 30))}`);
  }

  /* Lookahead only - never consumes: true if the next thing in the source
   * is an identifier immediately followed by a definition operator, i.e.
   * the start of a new rule. This is the only way to tell, in an
   * unterminated (no trailing ';') rule, where the current rule's
   * alternation actually ends. */
  function atNewRule() {
    const save = pos;

    skipWs();

    const name = peekIdent();

    if(name === null) {
      pos = save;
      return false;
    }

    let p = pos + name.length;

    while(p < len && /\s/.test(text[p])) p++;

    const isAssign = text.startsWith('::=', p) || text.startsWith(':=', p) || text[p] === ':';

    pos = save;
    return isAssign;
  }

  function readString() {
    const quote = text[pos];

    pos++;

    const start = pos;

    while(pos < len && text[pos] !== quote) pos++;

    const value = text.slice(start, pos);

    if(text[pos] === quote) pos++;

    return value;
  }

  function readCharClass() {
    pos++; // '['

    const start = pos;

    while(pos < len && text[pos] !== ']') pos++;

    const raw = text.slice(start, pos);

    if(text[pos] === ']') pos++;

    return new CharClass(parseCharClassBody(raw), '[' + raw + ']');
  }

  function readHex() {
    pos += 2; // '#x'

    const start = pos;

    while(isHexDigit(text[pos])) pos++;

    const hex = text.slice(start, pos);
    const lo = parseInt(hex, 16);

    if(text.startsWith('-#x', pos)) {
      pos += 3;

      const start2 = pos;

      while(isHexDigit(text[pos])) pos++;

      const hi = parseInt(text.slice(start2, pos), 16);

      return new CharClass(c => {
        const cp = c.codePointAt(0);
        return cp >= lo && cp <= hi;
      }, '#x' + hex);
    }

    return _lit(String.fromCodePoint(lo));
  }

  function parsePrimary() {
    skipWs();

    const c = text[pos];

    if(c === '(') {
      pos++;

      const rule = toAlternative(parseAlternation());

      skipWs();

      if(text[pos] === ')') pos++;

      return rule;
    }

    if(c === "'" || c === '"') return _lit(readString());

    if(c === '[') return readCharClass();

    if(text.startsWith('#x', pos)) return readHex();

    if(isIdentStart(c)) return grammar.ref(readIdent());

    throw new Error(`Unexpected input at offset ${pos}: ${JSON.stringify(text.slice(pos, pos + 30))}`);
  }

  function parsePostfix() {
    const primary = parsePrimary();

    skipWs();

    switch (text[pos]) {
      case '?':
        pos++;
        return new Optional(primary);

      case '*':
        pos++;
        return new ZeroOrMore(primary);

      case '+':
        pos++;
        return new OneOrMore(primary);
    }

    return primary;
  }

  function startsPrimary() {
    skipWs();

    const c = text[pos];

    return c === '(' || c === "'" || c === '"' || c === '[' || text.startsWith('#x', pos) || isIdentStart(c);
  }

  function parseSequence() {
    const items = [];

    for(;;) {
      skipWs();

      if(!startsPrimary()) break;

      if(text[pos] !== '(' && atNewRule()) break;

      if(text[pos] === '|' || text[pos] === ')' || text[pos] === ';') break;

      items.push(parsePostfix());
    }

    return items;
  }

  function parseAlternation() {
    const sequences = [parseSequence()];

    for(;;) {
      skipWs();

      if(text[pos] !== '|') break;

      pos++;
      sequences.push(parseSequence());
    }

    return sequences;
  }

  while(!atEnd()) {
    const name = readIdent();

    readAssign();

    const alternatives = parseAlternation();

    skipWs();

    if(text[pos] === ';') pos++;

    grammar.define(name, compileRule(name, alternatives));
  }

  return grammar;
}

/** Parses a grammar file's text (BNF, EBNF, or a simple YACC-style .y
 * grammar, see module doc comment above) into a Grammar of compiled Rule
 * objects. */
export function parseGrammar(text) {
  return parseGrammarSource(text);
}

/** Thin class wrapper around parseGrammar(), kept for call-site parity
 * with the old EBNFParser. */
export class EBNFParser {
  constructor(text) {
    this.grammar = parseGrammar(text);
  }

  get rules() {
    return this.grammar.rules;
  }

  get startRule() {
    return this.grammar.startRule;
  }
}

/** `buildGrammar(source, filename)`: parses `source`, annotating any parse
 * error with `filename` for a useful message, matching the old function's
 * call signature. */
export function buildGrammar(source, filename) {
  try {
    return parseGrammar(source);
  } catch(e) {
    if(filename) e.message = `${filename}: ${e.message}`;

    throw e;
  }
}

export function TryCatch(fn) {
  try {
    return fn();
  } catch(e) {
    return e;
  }
}

export default EBNFParser;
