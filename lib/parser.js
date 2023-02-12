import { extname } from 'path';

export function DumpToken(...args) {
  const { type, lexeme, loc } = args.pop();
  console.log(...args, (loc + '').padEnd(50), type.padEnd(20), lexeme.replace(/\n/g, '\\n'));
}

const Predicate = tok => rule_or_lexeme =>
  typeof rule_or_lexeme == 'function'
    ? rule_or_lexeme(tok)
    : +tok == rule_or_lexeme || tok.type == rule_or_lexeme || tok.lexeme == rule_or_lexeme;

export class Rule {
  match(lexer, fn = lex => false) {
    let pos = lexer.loc.clone();

    if(fn(lexer)) return true;

    lexer.pos = pos;

    return false;
  }
}
export class Terminal {
  constructor(id) {
    this.id = id;
  }

  match(lexer) {
    return super.match(lexer, lex => lex.next() == this.id);
  }
}

Rule.prototype[Symbol.operatorSet] = Operators.create(
  {
    '+': (a, b) => Symbol.for('Operator +'),
    '-': (a, b) => Symbol.for('Operator -'),
    '>>': (a, b) => Symbol.for('Operator >>'),
    '<<': (a, b) => Symbol.for('Operator <<'),
    '==': (a, b) => Symbol.for('Operator =='),
    '<': (a, b) => Symbol.for('Operator <'),
    '++': (a, b) => Symbol.for('Operator ++'),
    '--': (a, b) => Symbol.for('Operator --'),

    '^': (a, b) => Symbol.for('Operator ^'),
    '&': (a, b) => Symbol.for('Operator &'),
    '|': (a, b) => Symbol.for('Operator |')
  },
  {
    left: Number,
    '*'(a, b) {
      return Symbol.for('Rule.operator');
    }
  },
  {
    right: Number,
    '*'(a, b) {
      return Symbol.for('Rule.operator');
    }
  }
);

export class Parser {
  constructor(lexer) {
    this.lexer = lexer;
    this.buffer = [];
    this.processed = [];
    this.tokens = lexer ? lexer.tokens.reduce((acc, name, id) => ((acc[name] = id), acc), {}) : null;
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
