import { extname } from 'path';
import { define } from 'util';

export function DumpToken(...args) {
  const { type, lexeme, loc } = args.pop();
  console.log(...args, (loc + '').padEnd(50), type.padEnd(20), lexeme.replace(/\n/g, '\\n'));
}

const Predicate = tok => rule_or_lexeme => typeof rule_or_lexeme == 'function' ? rule_or_lexeme(tok) : +tok == rule_or_lexeme || tok.type == rule_or_lexeme || tok.lexeme == rule_or_lexeme;

export class Rule {
  match(lexer, fn = lex => false) {
    let pos = lexer.loc.clone();

    if(fn(lexer)) return true;

    lexer.pos = pos;

    return false;
  }
}

define(Rule.prototype, { [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'Rule' });

export class Terminal extends Rule {
  constructor(id, name) {
    super();
    this.id = id;
    this.name = name;
  }

  match(lexer) {
    return super.match(lexer, lex => lex.next() == this.id);
  }

  [Symbol.toPrimitive](hint) {
    switch (hint) {
      case 'number':
        return this.id;
      case 'string':
        return this.name;
    }
  }
}

define(Terminal.prototype, { [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'Terminal' });

export class ZeroOrMore extends Rule {
  constructor(id) {
    super();
    this.id = id;
  }

  match(lexer) {}

  [Symbol.toPrimitive](hint) {
    switch (hint) {
      case 'number':
        return this.id;
    }
  }
}

define(ZeroOrMore.prototype, { [Symbol.operatorSet]: Operators.create({}), [Symbol.toStringTag]: 'ZeroOrMore' });

export function make_operators_set(...op_list) {
  var new_op_list, i, a, j, b, k, obj, tab;
  var fields = ['left', 'right'];
  new_op_list = [];
  for(i = 0; i < op_list.length; i++) {
    a = op_list[i];
    if(a.left || a.right) {
      tab = [a.left, a.right];
      delete a.left;
      delete a.right;
      for(k = 0; k < 2; k++) {
        obj = tab[k];
        if(obj) {
          if(!Array.isArray(obj)) {
            obj = [obj];
          }
          for(j = 0; j < obj.length; j++) {
            b = {};
            Object.assign(b, a);
            b[fields[k]] = obj[j];
            new_op_list.push(b);
          }
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
    '~': (a, b) => Symbol.for('Operator ~')
  },
  {
    left: Number,
    '<<'(a, b) {
      return Symbol.for('Rule.operator');
    }
  },
  {
    right: Number,
    '<<'(a, b) {
      return Symbol.for('Rule.operator');
    }
  }
);

export class Parser {
  constructor(lexer) {
    this.lexer = lexer;
    this.buffer = [];
    this.processed = [];
    // this.tokens = lexer ? lexer.tokens.reduce((acc, name, id) => ((acc[name] = id), acc), {}) : null;
   
    let byName = {};
    let byId = [];

    for(let [id, name] of lexer.tokens.entries()) {
      byName[name] = byId[id] = new Terminal(id, name);
    }
    //
    this.rules = new Proxy(
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
            let id = byId.length;
            byName[prop] = byId[id] = value;
            return id;
          }
        },
        ownKeys(target) {
          return Object.getOwnPropertyNames(byName);
        },
        getPrototypeOf(target) {
          return Array.prototype;
        }
      }
    );
    //
    this.terminals = lexer ? lexer.tokens.reduce((acc, name, id) => ((acc[name] = new Terminal(id, name)), acc), {}) : null;
  }

  setInput(source, file) {
    const { lexer } = this;
    this.extname = extname(file);
    //console.log('extname', this.extname);
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
    //console.log("next", {tok});
    //console.log("buffer:", buffer, buffer.length);
    //DumpToken(tok);
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

    /*console.log('tok.id', tok.id);
      console.log('tok.type', tok.type);
      console.log('tokens', tokens);*/

    if(!ret && tokens.indexOf(tok.id) == -1) {
      const tokNames = tokens.map(tok => (typeof tok == 'number' ? this.lexer.tokens[tok] : tok));
      throw new Error(`${tok.loc} Expecting ${tokNames.join('|')}, got ${tok.type} '${tok.lexeme}'`);
    }

    return tok;
  }
}

globalThis.Parser = Parser;

export default Parser;
