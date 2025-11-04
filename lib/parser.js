import { extname } from 'path';
import { define, nonenumerable, declare } from 'util';
import { List } from 'list';

export function DumpToken(...args) {
  const { type, lexeme, loc } = args.pop();

  console.log(...args, (loc + '').padEnd(50), type.padEnd(20), lexeme.replace(/\n/g, '\\n'));
}

const Predicate = tok => rule_or_lexeme => (typeof rule_or_lexeme == 'function' ? rule_or_lexeme(tok) : +tok == rule_or_lexeme || tok.type == rule_or_lexeme || tok.lexeme == rule_or_lexeme);

export class Rule {
  static match(lexer, fn = lex => false) {
    const pos = lexer.loc.clone();

    if(fn(lexer)) return true;

    lexer.pos = pos;

    return false;
  }

  constructor(id) {
    this.id = id;
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

declare(Rule.prototype, { [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'Rule' });

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

declare(Terminal.prototype, { id: null, name: null, [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'Terminal', id: null, name: null });

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

declare(OneOrMore.prototype, { [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'OneOrMore' });

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

    '<<': (a, b) => Symbol.for('Operator <<'),
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
    declare(this, { lexer, buffer: new List(), processed: new List() });

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

    declare(this, {
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

    declare(this, {
      extname: extname(file),
    });

    return lexer.setInput(source, file);
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
      const { done, value } = lexer.next() ?? { done: true };
      if(done) return -1;
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
    const ret = tokens.some(Predicate(tok));

    if(!ret && tokens.indexOf(tok.id) == -1) {
      const tokNames = tokens.map(tok => (typeof tok == 'number' ? this.lexer.tokens[tok] : tok));
      throw new Error(`${tok.loc} Expecting ${tokNames.join('|')}, got ${tok.type} '${tok.lexeme}'`);
    }

    return tok;
  }
}

define(Parser.prototype, { [Symbol.toStringTag]: 'Parser' });

export default Parser;
