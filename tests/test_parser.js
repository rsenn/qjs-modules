import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import * as path from 'path';
import { Predicate } from 'predicate';
import { Location, Lexer, Token, SyntaxError } from 'lexer';
import Console from '../lib/console.js';
import JSLexer from '../lib/jslexer.js';
import CLexer from '../lib/clexer.js';
import BNFLexer from '../lib/bnflexer.js';
import Parser from '../lib/parser.js';
import extendArray from '../lib/extendArray.js';

('use math');

extendArray(Array.prototype);

function WriteFile(file, tok) {
  let f = std.open(file, 'w+');
  f.puts(tok);
  console.log('Wrote "' + file + '": ' + tok.length + ' bytes');
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({ start, pos, size })}`;
}

function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;

  return `★ Token ${inspect({ chars, offset, length, loc }, { depth: Infinity })}`;
}

/*Token.prototype.inspect = function(options = {}) {
  const { byteLength,start,length,offset,lexeme,loc} = this;
  return inspect({ byteLength, start, length,offset,lexeme,loc});
}
*/
class EBNFParser extends Parser {
  constructor() {
    super(new BNFLexer());
  }

  setInput(source, file) {
    const { lexer } = this;
    return lexer.setInput(source, file);
  }

  parseDirective() {
    let tok = this.expect('directive');

    while(this.match(tok => tok.type == 'identifier')) {
      this.expect('identifier');
    }
    while(this.match('newline')) this.expect('newline');
  }

  parseProduction() {
    let id = this.expect('identifier');
    let toks = [];
    this.expect('colon');

    while(!this.match('semi')) toks.push(this.expect(tok => true));

    this.expect('semi');
    console.log(id.lexeme + ':');
    console.log(toks.map(t => `\t${t.type.padEnd(20)} ${t.lexeme}`).join('\n'));
  }

  parse() {
    while(this.match('directive')) this.parseDirective();

    this.expect('section');

    while(!this.match('section')) {
      this.parseProduction();
    }
  }
}

async function main(...args) {
  new Console({
    colors: true,
    depth: 8,
    maxArrayLength: 100,
    maxStringLength: Infinity,
    compact: 1,
    showHidden: false,
    customInspect: true
  });

  let file = args[0] ?? 'tests/ANSI-C-grammar-2011.y';
  console.log('file:',file);
  let str = std.loadFile(file, 'utf-8');
  let len = str.length;
  let type = path.extname(file).substring(1);

  let parser = new EBNFParser();

  parser.setInput(str, file);

  let ast = parser.parse();

  console.log('ast:', ast);

  std.gc();
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
