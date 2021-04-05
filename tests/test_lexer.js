import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Predicate } from 'predicate.so';
import { Lexer } from 'lexer.so';
import Console from './console.js';

('use strict');
('use math');

globalThis.inspect = inspect;

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log(`Wrote '${file}': ${data.length} bytes`);
}
function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({
    size,
    pos,
    start /*, line, column, lineStart, lineEnd, columnIndex*/
  })}`;
}
function DumpToken(tok) {
  const { length, offset, loc } = tok;

  return `Token ${inspect({ length, offset, loc }, { depth: Infinity })}`;
}

function main(...args) {
  console = new Console({
    colors: true,
    depth: 8,
    maxArrayLength: 100,
    maxStringLength: 100,
    compact: false
  });
  let str = std.loadFile(args[0] ?? scriptArgs[0], 'utf-8');
  let len = str.length;
  console.log('len', len);
  console.log('str', str);
  let lexer = new Lexer(str, len);

  // /* prettier-ignore */ lexer.keywords = ['if', 'in', 'do', 'of', 'as', 'for', 'new', 'var', 'try', 'let', 'else', 'this', 'void', 'with', 'case', 'enum', 'from', 'break', 'while', 'catch', 'class', 'const', 'super', 'throw', 'await', 'yield', 'async', 'delete', 'return', 'typeof', 'import', 'switch', 'export', 'static', 'default', 'extends', 'finally', 'continue', 'function', 'debugger', 'instanceof'];
  // /* prettier-ignore */ lexer.punctuators = [ '!', '!=', '!==', '${', '%', '%=', '&&', '&&=', '&', '&=', '(', ')', '*', '**', '**=', '*=', '+', '++', '+=', ',', '-', '--', '-->>', '-->>=', '-=', '.', '...', '/', '/=', ':', ';', '<', '<<', '<<=', '<=', '=', '==', '===', '=>', '>', '>=', '>>', '>>=', '>>>', '>>>=', '?', '?.', '??', '??=', '@', '[', '^', '^=', '{', '|', '|=', '||', '||=', '}', '~'];
  //  console.log('lexer.peek()', lexer.peek());
  //console.log('lexer.next()', lexer.next());
  lexer.lexNumber = function lexNumber() {};

  function lexText() {
    do {
      //Examine the next 2 characters to see if we're encountering code comments
      const nextTwo = this.getRange(this.pos, this.pos + 2);
      if(nextTwo === '//') {
        this.skip(2);
        return this.lexSingleLineComment;
      } else if(nextTwo === '/*') {
        this.skip(2);
        return this.lexMultiLineComment;
      }

      //Consume the next character and decide what to do
      const c = this.getc();
      if(c === null) {
        //EOF
        return null;
      } else if(!this.noRegex && isRegExpChar(c)) {
        return this.lexRegExp;
      } else if(isQuoteChar(c)) {
        return this.lexQuote(c);
      } else if(isDecimalDigit(c) || (c === '.' && isDecimalDigit(this.peek()))) {
        this.backup();
        return this.lexNumber;
      } else if(isWhitespace(c)) {
        this.ignore();
      } else if(isPunctuatorChar(c)) {
        this.backup();
        return this.lexPunctuator;
      } else if(isIdentifierChar(c)) {
        this.backup();
        return this.lexIdentifier;
      } else if(isLineTerminator(c)) {
        this.ignore();
      } else {
        throw this.error(`Unexpected character: ${c}`);
      }
    } while(true);
  }
  lexer.lexText = lexText;

  lexer.stateFn = lexText;

  lexer.acceptRun(new Predicate(/^[A-Za-z_]$/));
  console.log('lexer', DumpLexer(lexer));

  //lexer.acceptRun(c => /^\s/.test(c));
  lexer.acceptRun(new Predicate(/^\s$/));
  console.log('lexer', DumpLexer(lexer));

  let data;
  for(let data of lexer) {
    console.log('data', data.toString());

    /* lexer.acceptRun(Lexer.isWhitespace);

    console.log('lexer', DumpLexer(lexer));*/
  }

  std.gc();
}

main(...scriptArgs.slice(1));
