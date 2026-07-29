import { extname } from 'path';
import { extend } from 'util';
import { List } from 'list';

export function DumpToken(...args) {
  const { type, lexeme, loc } = args.pop();

  console.log(...args, (loc + '').padEnd(50), type.padEnd(20), lexeme.replace(/\n/g, '\\n'));
}

const Predicate = tok => rule_or_lexeme => (typeof rule_or_lexeme == 'function' ? rule_or_lexeme(tok) : +tok == rule_or_lexeme || tok?.type == rule_or_lexeme || tok?.lexeme == rule_or_lexeme);

export class Rule {
  static match(lexer, fn = lex => false) {
    const pos = lexer.loc.clone();

    if(fn(lexer)) return true;

    lexer.charPos = pos;

    return false;
  }

  constructor(id) {
    if(id !== undefined) this.id = id;
  }

  match(lexer) {
    return Rule.match(lexer, lex => lex.next() == this.id);
  }

  [Symbol.toPrimitive](hint) {
    switch (hint) {
      case 'number':
        return this.id;
    }
  }
}

/* Every Rule subclass below deliberately does *not* get its own
 * [Symbol.operatorSet] - they all inherit this one, via the prototype
 * chain, so that e.g. `terminalA << terminalB` and `terminalA << oneOrMore`
 * both resolve through the "self operators" table (same operator_counter),
 * regardless of which concrete Rule subclass either side is. See
 * doc/operator-overloading.md and doc/predicate.md for how this dispatch
 * works in general. */
extend(Rule.prototype, { [Symbol.toStringTag]: 'Rule' });

export class Terminal extends Rule {
  constructor(id, name) {
    super(id);
    this.name = name;
  }

  [Symbol.toPrimitive](hint) {
    switch (hint) {
      case 'string':
        return this.name;

      default:
        return Rule.prototype[Symbol.toPrimitive].call(this, hint);
    }
  }
}

extend(Terminal.prototype, { id: null, name: null, [Symbol.toStringTag]: 'Terminal' });

/**
 * boost::spirit's `+rule` (Kleene plus): matches `rule` one or more times,
 * greedily. Built by unary `+` (see the `pos` operator below). Wraps an
 * arbitrary Rule now, not a raw token/char id like the original
 * id-comparing OneOrMore this replaced.
 */
export class OneOrMore extends Rule {
  constructor(rule) {
    super();
    this.rule = rule;
  }

  match(lexer) {
    return Rule.match(lexer, lex => {
      if(!this.rule.match(lex)) return false;
      while(this.rule.match(lex)) {}
      return true;
    });
  }
}

extend(OneOrMore.prototype, { [Symbol.toStringTag]: 'OneOrMore' });

/**
 * boost::spirit's `-rule` (optional/"ZeroOrOneTime"): matches `rule` zero or
 * one times and always succeeds itself - a failed attempt at `rule` is not a
 * failure of the Optional, it just means "0 times" (`rule.match()` already
 * rewinds itself on failure, so there's nothing left to undo). Built by
 * unary `-` (see the `neg` operator below).
 */
export class Optional extends Rule {
  constructor(rule) {
    super();
    this.rule = rule;
  }

  match(lexer) {
    this.rule.match(lexer);
    return true;
  }
}

extend(Optional.prototype, { [Symbol.toStringTag]: 'Optional' });

/**
 * A composed rule that matches its sub-rules in order (the `<<`/`>>`
 * operators below build these), rewinding the lexer to where the sequence
 * started if any sub-rule fails - not just the failing sub-rule's own start
 * position, which its own `.match()` already rewound to. Every sub-rule
 * need only implement `.match(lexer)`; `Sequence` doesn't care whether it's
 * a `Terminal`, `OneOrMore`, or another `Sequence`.
 *
 * A plain function may also appear as one of the rules - a semantic action,
 * e.g. `_char('{') >> function action() {} >> _char('}')` - run in place
 * (called with the lexer/input) instead of matched. An action's return
 * value is ignored unless it's exactly `false`, in which case it behaves
 * like a failed sub-rule and fails (and backtracks) the whole Sequence -
 * this lets an action double as a semantic predicate, but a typical
 * side-effecting action (build an AST node, push a value, ...) can just
 * fall off the end and always "succeeds".
 */
export class Sequence extends Rule {
  constructor(...rules) {
    super();
    this.rules = rules;
  }

  match(lexer) {
    return Rule.match(lexer, lex => this.rules.every(rule => (typeof rule == 'function' ? rule(lex) !== false : rule.match(lex))));
  }
}

extend(Sequence.prototype, { [Symbol.toStringTag]: 'Sequence' });

/* boost::spirit's char_('x'): matches a single element equal to `ch` from
 * whatever "lexer" it's given - a real token Lexer (comparing against a
 * token id) or a plain character stream such as stringInput() below
 * (comparing character-by-character); Rule's own default .match() already
 * does exactly this comparison (`lex.next() == this.id`), so _char() is
 * just Rule construction under a boost::spirit-flavored name. */
export function _char(ch) {
  return new Rule(ch);
}

/* A minimal character-stream "lexer" for _char()/Sequence/etc. to run
 * against directly, without going through the token-oriented Lexer class
 * (quickjs-lexer.c) at all - satisfies the same duck-typed interface
 * Rule.match() relies on (.loc.clone() / .charPos) for backtracking. */
export function stringInput(str) {
  let i = 0;

  return {
    next() {
      return i < str.length ? str[i++] : undefined;
    },
    get loc() {
      return { clone: () => i };
    },
    get charPos() {
      return i;
    },
    set charPos(value) {
      i = value;
    },
    get eof() {
      return i >= str.length;
    },
  };
}

export function make_operators_set(...op_list) {
  let obj;
  const new_op_list = [],
    fields = ['left', 'right'];

  for(let i = 0; i < op_list.length; i++) {
    const a = op_list[i];

    if(a.left || a.right) {
      const tab = [a.left, a.right];

      delete a.left;
      delete a.right;

      for(let k = 0; k < 2; k++)
        if((obj = tab[k])) {
          if(!Array.isArray(obj)) obj = [obj];

          for(let j = 0; j < obj.length; j++) {
            const b = {};
            Object.assign(b, a);
            b[fields[k]] = obj[j];
            new_op_list.push(b);
          }
        }
    } else {
      new_op_list.push(a);
    }
  }

  return new_op_list;
}

/* boost::spirit's `>>` sequencing operator (also usable as `<<`, spirit's
 * own *karma* - generator-side - spelling of the same idea): `a >> b`
 * builds a Sequence matching `a` then `b`, in order, backtracking the whole
 * thing if either fails (see Sequence above). A plain function operand
 * (`a >> function() {}`) becomes a semantic action rather than a sub-rule; a
 * bare number literal (via the Number cross-type entries below, which
 * already carries a primitive operator set installed by
 * JS_AddIntrinsicOperators(), so no extra setup is needed the way Function
 * required) is auto-wrapped the same way `_char()` wraps one -
 * `_char('{') >> 5 >> _char('}')` needs no explicit `new Rule(5)`. Only
 * Number, not String: `<<`/`>>` are bitwise ops, and QuickJS's operator
 * dispatch runs any *primitive*-flagged cross-type operand through
 * JS_ToNumeric() before calling this function at all (see
 * doc/operator-overloading.md step 5) - harmless for Number (a no-op), but
 * it silently turns a string operand into NaN, so String is deliberately
 * not wired up below.
 * Left-associative chains (`a >> b >> c`) flatten into one Sequence of
 * three entries rather than nesting, same as `a.then(b).then(c)` in
 * lib/parser/grammar.js's (operator-free) equivalent. */
function sequence(a, b) {
  const flatten = x => {
    if(x instanceof Sequence) return x.rules;
    if(typeof x == 'function' || x instanceof Rule) return [x];
    return [new Rule(x)];
  };

  return new Sequence(...flatten(a), ...flatten(b));
}

/* Function objects (e.g. a bare `function action() {}` written inline in a
 * `>>` chain) don't carry a [Symbol.operatorSet] of their own, so mixing one
 * into a Rule expression needs Function.prototype to have one - empty
 * self-ops (Function >> Function has no meaning here), just so it has an
 * operator_counter for Rule's cross-type left/right tables below to
 * reference. Must be installed *before* Rule.prototype's own operator set,
 * so Rule's is the higher (later-assigned) operator_counter of the two -
 * see doc/predicate.md for why that's what makes Rule's own left/right
 * tables (not Function's) the ones consulted, regardless of which literal
 * side of the expression a Rule instance is on. */
Function.prototype[Symbol.operatorSet] = Operators.create({});

Rule.prototype[Symbol.operatorSet] = Operators.create(
  {
    '+': (a, b) => Symbol.for('Operator +'),
    '-': (a, b) => Symbol.for('Operator -'),
    '*': (a, b) => Symbol.for('Operator *'),
    '/': (a, b) => Symbol.for('Operator /'),
    '%': (a, b) => Symbol.for('Operator %'),

    '**': (a, b) => Symbol.for('Operator **'),
    '|': (a, b) => Symbol.for('Operator |'),
    '&': (a, b) => Symbol.for('Operator &'),
    '^': (a, b) => Symbol.for('Operator ^'),

    '<<': sequence,
    '>>': sequence,
    '>>>': (a, b) => Symbol.for('Operator >>>'),
    '==': (a, b) => Symbol.for('Operator =='),
    '<': (a, b) => Symbol.for('Operator <'),
    /* unary +rule: one-or-more (boost::spirit's Kleene plus) */
    pos: a => new OneOrMore(a),
    /* unary -rule: zero-or-one ("ZeroOrOneTime", boost::spirit's optional) */
    neg: a => new Optional(a),
    '++': (a, b) => Symbol.for('Operator ++'),
    '--': (a, b) => Symbol.for('Operator --'),
    '~': (a, b) => Symbol.for('Operator ~'),
  },
  { left: Number, '<<': sequence, '>>': sequence },
  { right: Number, '<<': sequence, '>>': sequence },
  { left: Function, '<<': sequence, '>>': sequence },
  { right: Function, '<<': sequence, '>>': sequence },
);

export class Parser {
  constructor(lexer) {
    extend(this, { lexer, buffer: new List(), processed: new List() });

    // this.tokens = lexer ? lexer.tokens.reduce((acc, name, id) => ((acc[name] = id), acc), {}) : null;

    const byName = {},
      byId = [];

    if(this.lexer)
      this.lexer.handler = (arg, tok) => {
        const line = arg.currentLine();
        const index = arg.loc.column - 1;
        const error = new Error(
          `Unmatched token at ${arg.loc} char='${BNFLexer.escape(line[index])}' section=${this.section} state=${arg.states[arg.state]}\n${line}\n${[...line]
            .slice(0, index)
            .map(c => (c != '\t' ? ' ' : c))
            .join('')}^`,
        );

        console.log(error.message);

        // console.log('tokens', [...this.processed, ...this.buffer].slice(-10).map(InspectToken));

        throw error;
      };

    if(lexer?.tokens) for(const [id, name] of lexer.tokens.entries()) byName[name] = byId[id] = new Terminal(id, name);

    extend(this, {
      rules: new Proxy(
        {},
        {
          get(target, prop, receiver) {
            if(prop == 'length') return byId.length;
            if(!isNaN(+prop)) return byId[prop];
            return byName[prop];
          },
          has(target, prop) {
            if(!isNaN(+prop)) return prop in byId;
            return prop in byName;
          },
          set(target, prop, value) {
            if(!(prop in byName)) {
              const id = byId.length;
              byName[prop] = byId[id] = value;
              return id;
            }
          },
          ownKeys(target) {
            return Object.getOwnPropertyNames(byName);
          },
          getPrototypeOf(target) {
            return Array.prototype;
          },
        },
      ),
      terminals: lexer ? lexer.tokens.reduce((acc, name, id) => ((acc[name] = new Terminal(id, name)), acc), {}) : null,
    });
  }

  setInput(source, file) {
    const { lexer } = this;

    extend(this, {
      extname: extname(file),
    });

    return lexer.setInput(source, file);
  }

  get tokens() {
    return this.lexer.tokens.reduce((o, tok, i) => ((o[tok] = i), o), {});
  }

  consume() {
    const { buffer, processed } = this;
    if(buffer.length === 0) this.next();
    const tok = buffer.shift();
    //if(buffer.length == 0) this.next();
    processed.push(tok);
    return tok;
  }

  next() {
    let tok;
    const { buffer, lexer } = this;

    while(buffer.length == 0) {
      const value = lexer.nextToken();

      if(!value) break;

      buffer.push(value);
    }

    if(buffer.length > 0) tok = buffer[0];

    return tok;
  }

  match(tokens) {
    const tok = this.next();

    if(!Array.isArray(tokens)) tokens = [tokens];
    if(tok && tokens.some(Predicate(tok))) return tok;

    return null;
  }

  expect(tokens) {
    const tok = this.consume();

    if(!Array.isArray(tokens)) tokens = [tokens];

    if(tok) {
      const ret = tokens.some(Predicate(tok));

      if(!ret && tokens.indexOf(tok.id) == -1) {
        const tokNames = tokens.map(tok => (typeof tok == 'number' ? this.lexer.tokens[tok] : tok));
        throw new Error(`${tok.loc} Expecting ${tokNames.join('|')}, got ${tok.type} '${tok.lexeme}'`);
      }
    } else {
      const tokNames = tokens.map(tok => (typeof tok == 'number' ? this.lexer.tokens[tok] : tok));
      throw new Error(`${this.lexer.loc} Expecting ${tokNames.join('|')}, got ${tok}`);
    }

    return tok;
  }
}

extend(Parser.prototype, { [Symbol.toStringTag]: 'Parser' });

export default Parser;