import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Predicate } from 'predicate.so';
import { Lexer, Token, SyntaxError } from 'lexer.so';
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
    start,
    pos,
    size /*, line, column, lineStart, lineEnd, columnIndex*/
  })}`;
}
function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;

  return `Token ${inspect({ chars, offset, length, loc }, { depth: Infinity })}`;
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
  let lexer = new Lexer(str, len);

  lexer.lexNumber = function lexNumber() {};
  const isPunctuator = c => /^[-=.%}>,*<!/~&\(;?|\):+^{@]$/.test(c);
  function lexText() {
    do {
      const nextTwo = this.getRange(this.pos, this.pos + 2);
      if(nextTwo === '//') {
        this.skip(2);
        return this.lexSingleLineComment;
      } else if(nextTwo === '/*') {
        this.skip(2);
        return this.lexMultiLineComment;
      }

      const c = this.getc();
      if(c === null) {
        return null;
      } else if(!this.noRegex && /^\/$/.test(c)) {
        return this.lexRegExp;
      } else if(/^['"`]$/.test(c)) {
        this.backup();
        return this.lexQuote;
      } else if(/^[0-9]$/.test(c) || (c === '.' && /^[0-9]$/.test(this.peek()))) {
        this.backup();
        return this.lexNumber;
      } else if(/^\s$/.test(c)) {
        this.ignore();
      } else if(isPunctuator(c)) {
        this.backup();
        return this.lexPunctuator;
      } else if(/^[A-Za-z_]$/.test(c)) {
        this.backup();
        return this.lexIdentifier;
      } else if(c == '\n') {
        this.ignore();
      } else {
        return this.error(`Unexpected character: ${c}`);
        break;
      }
    } while(true);
  }

  function lexRegExp() {
    let i = 0;
    let word = '',
      prev = '';
    let slashes = 1;
    let bracket = false;
    let validator = c => {
      i++;
      if(c == '[' && prev != '\\') if (!bracket) bracket = true;
      if(c == ']' && prev != '\\') if (bracket) bracket = false;

      if(((i == 1 && /^\s$/.test(c)) || c == '\n') && prev != '\\') {
        return false;
      } else if(slashes == 1 && c == ' ' && prev == '/') {
        return false;
      } else if(c == '/' && prev != '\\' && !bracket) {
        slashes++;
      } else if(c == 'n' && prev == '\\') {
        word += '\n';
        prev = c;
        return true;
      } else if(prev == '\\') {
        word += c;
        prev = undefined;
        return true;
      } else if(slashes == 2 && ' \t'.indexOf(c) != -1) {
        return true;
      } else if(slashes == 2 && 'gimsuy'.indexOf(c) != -1) {
      } else if(slashes == 2) {
        if(/^[_0-9A-Za-z]/.test(c)) slashes = 1;
        return false;
      } else if(c == '\\') {
      }
      word += c;
      prev = c;
      return true;
    };
    const print = () => {
      word = this.getRange(this.start, this.pos);
    };

    if(this.acceptRun(validator) && slashes == 2) {
      print();
      this.addToken(Token.REGEXP_LITERAL);
      return this.lexText;
    }
    this.backup(this.pos - this.start - 1);
    return this.lexPunctuator();
  }
  function lexPunctuator() {
    for(;;) {
      //     console.log('lexPunctuator', this.peek().codePointAt(0), isPunctuator(this.peek()));

      if(!this.accept(isPunctuator)) break;

      let word = this.getRange(this.start, this.pos);
      //console.log(`word(1) '${word}'`, isPunctuator(word));
      if(word != '..' && !isPunctuator(word)) {
        this.backup();
        this.addToken(Token.PUNCTUATOR);
        return this.lexText;
      }
    }
    const word = this.getRange(this.start, this.pos);

    //console.log(`word(2) '${word}'`);

    if(isPunctuator(word)) {
      this.addToken(Token.PUNCTUATOR);
      return this.lexText;
    }
    return this.error(`Invalid PUNCTUATOR: ${word}`);
  }
  function lexTemplate(cont = false) {
    const done = (doSubst, defaultFn = null, level) => {
      let self = () => {
        let c = this.peek();
        let { start, pos } = this;
        const position = this.position;
        const { stateFn } = this;
        if(c == ';') throw new Error(`${this.position}`);
        if(!doSubst && c == '`') {
          this.template = null;
          this.addToken(Token.TEMPLATE_LITERAL);
          return this.lexText();
        }
        let fn = doSubst == this.inSubst ? this.lexText : defaultFn;
        let ret;
        if(doSubst && c == '}') {
          c = this.peek();
          this.inSubst--;
          return fn;
        }
        if(fn === null) throw new Error();
        return fn;
      };
      return self;
    };
    let prevChar = this.peek();
    let c;
    let startToken = this.tokenIndex;
    function template() {
      let escapeEncountered = false;
      let n = 0;
      do {
        if(this.acceptRun(not(or(c => c === '$', oneOf('\\`{$'))))) escapeEncountered = false;
        prevChar = c;
        c = this.getc();
        ++n;
        if(c === null) {
          return this.error(`Illegal template token (${n})  '${this.source[this.start]}'`);
        } else if(!escapeEncountered) {
          if(c == '{' && prevChar == '$') {
            this.backup(2);
            this.addToken(Token.TEMPLATE_LITERAL);
            this.skip(2);
            this.ignore();
            this.inSubst = (this.inSubst || 0) + 1;
            return done(this.inSubst, this.lexTemplate);
          } else if((cont || !this.inSubst) && c === '`') {
            this.inSubst = cont - 1;
            this.addToken(Token.TEMPLATE_LITERAL);
            return this.lexText.bind(this);
          } else if(c === '\\') {
            escapeEncountered = true;
          }
        } else {
          escapeEncountered = false;
        }
      } while(true);
    }
    return template.call(this);
  }

  function lexIdentifier() {
    //console.log('lexIdentifier(1)', DumpLexer(this));
    this.acceptRun(c => /^[A-Za-z0-9_]$/.test(c));
    const firstChar = this.getRange(this.start, this.start + 1);
    if(/^[0-9]$/.test(firstChar)) return this.error(`Invalid IDENTIFIER\n`);
    const c = this.peek();
    if(c == '`') {
      const { pos, start } = this;
      this.addToken(Token.IDENTIFIER);
      return this.lexText;
    }
    if(/^['"]$/.test(c)) return this.error(`Invalid IDENTIFIER`);
    const word = this.getRange(this.start, this.pos);
    //console.log(`word '${word}'`);
    if(word === 'true' || word === 'false') this.addToken(Token.BOOLEAN_LITERAL);
    else if(word === 'null') this.addToken(Token.NULL_LITERAL);
    // else if(isKeyword(word)) this.addToken(Token.KEYWORD);
    else {
      this.addToken(Token.IDENTIFIER);
    }

    return this.lexText;
  }

  lexer.lexText = lexText;
  lexer.lexQuote = function lexQuote() {
    let quoteChar = this.getc();
    if(quoteChar === '`') {
      const { inSubst } = this;
      return this.lexTemplate(inSubst);
    }
    //console.log(`lexQuote <${quoteChar}>`);
    let prevChar = '';
    let c = '';
    let escapeEncountered = false;
    do {
      if(this.acceptRun(
          Predicate.not(Predicate.or(c => c == '\n', Predicate.charset(`\\${quoteChar}`)))
        )
      )
        escapeEncountered = false;
      prevChar = c;
      c = this.getc();

      if(c === null) {
        return this.error(`Illegal token(1)`);
        
      } else if(!escapeEncountered) {
        if(c == '\n' && quoteChar !== '`') {
          throw   this.error(`Illegal token(2) c=0x${c.codePointAt(0).toString(16)} quoteChar=${quoteChar} `);
          break;
        } else if(c === quoteChar) {
          this.addToken(Token.STRING_LITERAL);
          return this.lexText;
        } else if(c === '\\') {
          escapeEncountered = true;
        }
      } else {
        escapeEncountered = false;
      }
    } while(true);
  };

  lexer.lexRegExp = lexRegExp;
  lexer.lexPunctuator = lexPunctuator;
  lexer.lexTemplate = lexTemplate;
  lexer.lexIdentifier = lexIdentifier;

  lexer.stateFn = lexText;

  /* lexer.acceptRun(new Predicate(/^[A-Za-z_]$/));
  console.log('lexer', DumpLexer(lexer));
  
   lexer.acceptRun(new Predicate(/^\s$/));
  console.log('lexer', DumpLexer(lexer));
*/
  let data;
  
  for(let data of lexer) {
    console.log(`data `, data);

    if(data == null) {
      console.log("Exception:", lexer.exception);
      break;
    }

    /* lexer.acceptRun(Lexer.isWhitespace);

    console.log('lexer', DumpLexer(lexer));*/
  }

  std.gc();
}

main(...scriptArgs.slice(1));
