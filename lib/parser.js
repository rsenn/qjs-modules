function DumpToken(tok) {
  const { type, lexeme, loc } = tok;

  console.log(loc.toString().padEnd(50), type.padEnd(20), lexeme.replace(/\n/g, '\\n'));
}
export class Parser {
  tokens = [];
  processed = [];

  constructor(lexer) {
    this.lexer = lexer;
  }

  consume() {
    const { tokens, processed } = this;
    if(tokens.length === 0) this.next();
    const token = tokens.shift();
    //if(tokens.length == 0) this.next();
    processed.push(token);
    return token;
  }

  next() {
    let token;
    const { tokens, lexer } = this;
    if(tokens.length) token = tokens[0];
    else {
      token = lexer.lex();
      tokens.push(token);
      DumpToken(token);
    }
    return token;
  }

  match(tokens) {
    const tok = this.next();
    if(!Array.isArray(tokens)) tokens = [tokens];
    if(tokens.some(rule_or_lexeme =>
        typeof rule_or_lexeme == 'function'
          ? rule_or_lexeme(tok)
          : tok.type == rule_or_lexeme || tok.lexeme == rule_or_lexeme
      )
    )
      return tok;
    return null;
  }

  expect(tokens) {
    const tok = this.consume();
    if(!Array.isArray(tokens)) tokens = [tokens];
    const ret = tokens.some(rule_or_lexeme =>
      typeof rule_or_lexeme == 'function'
        ? rule_or_lexeme(tok)
        : tok.type == rule_or_lexeme || tok.lexeme == rule_or_lexeme
    );

    if(!ret)
      throw new Error(`${tok.loc} Expecting ${tokens.join('|')}, got ${tok.type} '${tok.lexeme}'`);
    return tok;
  }
}

export default Parser;
