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

export class OneOrMore extends Rule {
  constructor(id) {
    super();
    this.id = id;
  }

  match(lexer) {
    let ret = super.match(lexer);
    while(super.match(lexer)) {}
    return ret;
  }
}

extend(OneOrMore.prototype, { [Symbol.toStringTag]: 'OneOrMore' });

/**
 * A composed rule that matches its sub-rules in order (the `<<` operator
 * below builds these), rewinding the lexer to where the sequence started if
 * any sub-rule fails - not just the failing sub-rule's own start position,
 * which its own `.match()` already rewound to. Every sub-rule need only
 * implement `.match(lexer)`; `Sequence` doesn't care whether it's a
 * `Terminal`, `OneOrMore`, or another `Sequence`.
 */
export class Sequence extends Rule {
  constructor(...rules) {
    super();
    this.rules = rules;
  }

  match(lexer) {
    return Rule.match(lexer, lex => this.rules.every(rule => rule.match(lex)));
  }
}

extend(Sequence.prototype, { [Symbol.toStringTag]: 'Sequence' });

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

    /* boost::spirit's `>>` sequencing operator, spelled `<<` here: `a << b`
     * builds a Sequence that matches `a` then `b`, in order, backtracking
     * the whole thing if either fails (see Sequence above). Left-associative
     * chains (`a << b << c`) flatten into one Sequence of three rules rather
     * than nesting, same as `a.then(b).then(c)` in lib/parser/grammar.js's
     * (operator-free) equivalent. */
    '<<': (a, b) => new Sequence(...(a instanceof Sequence ? a.rules : [a]), ...(b instanceof Sequence ? b.rules : [b])),
    '>>': (a, b) => Symbol.for('Operator >>'),
    '>>>': (a, b) => Symbol.for('Operator >>>'),
    '==': (a, b) => Symbol.for('Operator =='),
    '<': (a, b) => Symbol.for('Operator <'),
    pos: a => Symbol.for('Operator pos'),
    neg: a => Symbol.for('Operator neg'),
    '++': (a, b) => Symbol.for('Operator ++'),
    '--': (a, b) => Symbol.for('Operator --'),
    '~': (a, b) => Symbol.for('Operator ~'),
  },
  {
    left: Number,
    '<<'(a, b) {
      return Symbol.for('Rule.operator');
    },
  },
  {
    right: Number,
    '<<'(a, b) {
      return Symbol.for('Rule.operator');
    },
  },
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