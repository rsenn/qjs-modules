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
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
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
  let file =args[0] ?? scriptArgs[0];
  let str = std.loadFile(file, 'utf-8');
  let len = str.length;
  console.log('len', len);
  let lexer = new Lexer(str, file);

  const isKeyword = word =>
  //  /^(if|in|do|of|as|for|new|var|try|let|else|this|void|with|case|enum|from|break|while|catch|class|const|super|throw|await|yield|async|delete|return|typeof|import|switch|export|static|default|extends|finally|continue|function|debugger|instanceof)$/.test(word);
  /^(_Alignas|_Alignof|_Atomic|_Bool|_Complex|_Generic|_Imaginary|_Noreturn|_Static_assert|_Thread_local|auto|break|case|char|const|continue|default|do|double|else|enum|extern|float|for|goto|if|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile)$/.test(word);

  const isPunctuator = word =>
    /^(=|\.|-|%|}|>|,|\*|\[|<|!|\/|\]|~|\&|\(|;|\?|\||\)|:|\+|\^|{|@|!=|\*=|\&\&|<<|\/=|\|\||>>|\&=|==|\+\+|\|=|<=|--|\+=|\^=|>=|-=|%=|=>|\${|\?\.|\*\*|\?\?|!==|===|>>>|>>=|-->>|<<=|\.\.\.|\*\*=|\|\|=|\&\&=|\?\?=|>>>=|-->>=)$/.test(word
    );
  const isIdentifierChar = c => Lexer.isIdentifierChar(c) || c == '#';
  const isIdentifierFirstChar = c => Lexer.isIdentifierFirstChar(c) || c == '#';

  function lexText() {
    do {
      const nextTwo = this.getRange(this.pos, this.pos + 2);
      if(nextTwo === '//') {
        this.skip(2);
        return this.lexSingleLineComment;
      } else if(nextTwo === '/*') {
        this.skip(2);
        return this.lexMultiLineComment;
      } else if(nextTwo === '\\\n') {
        this.skip(2);
        this.ignore();
        continue;
        return this.lexText;
      }
      const c = this.getc();
      if(c === null) {
        return null;
      } else if(!this.noRegex && Lexer.isRegExpChar(c)) {
        return this.lexRegExp;
      } else if(c == '`') {
        return this.lexTemplate;
      } else if(Lexer.isQuoteChar(c)) {
        this.backup();
        return this.lexQuote;
      } else if(Lexer.isDecimalDigit(c) || (c === '.' && Lexer.isDecimalDigit(this.peek()))) {
        this.backup();
        return this.lexNumber;
      } else if(Lexer.isWhitespace(c)) {
        this.ignore();
      } else if(Lexer.isPunctuatorChar(c)) {
        this.backup();
        return this.lexPunctuator;
      } else if(isIdentifierFirstChar(c)) {
        this.backup();
        return this.lexIdentifier;
      } else if(Lexer.isLineTerminator(c)) {
        this.ignore();
      } else {
        throw this.error(`Unexpected character: ${c}`);
        break;
      }
    } while(!this.eof);
  }

  function lexNumber() {
    let validator = Lexer.isDecimalDigit;

    if(this.accept(c => c == '0' /*Predicate.charset('0000')*/)) {
      if(this.accept(c => c == 'x' || c == 'X' /*Predicate.charset('xX')*/)) {
        validator = Lexer.isHexDigit;

        if(!this.accept(validator))
          throw this.error(`Invalid number (x): ${this.getRange(this.start, this.pos + 1)}`);
      } else if(this.accept(Predicate.charset('oO'))) {
        validator = Lexer.isOctalDigit;
        if(!this.accept(validator))
          throw this.error(`Invalid number (o): ${this.getRange(this.start, this.pos + 1)}`);
      } else if(this.accept(Lexer.isOctalDigit)) {
        validator = Lexer.isOctalDigit;
      } else if(this.accept(Lexer.isDecimalDigit)) {
        throw this.error(`Invalid number (1): ${this.getRange()}`);
      }
    }
    this.acceptRun(validator);
    if(validator == Lexer.isDecimalDigit) {
      if(this.accept(Predicate.charset('.'))) this.acceptRun(validator);
      if(this.accept(Predicate.charset('eE'))) {
        this.accept(Predicate.charset('+-'));
        if(!this.accept(validator))
          throw this.error(`Invalid number (2): ${this.getRange(this.start, this.pos + 1)}`);
        this.acceptRun(validator);
      }
    }
    const c = this.peek();
    /* l = BigFloat, m = BigDecimal, n = BigInt */
    if(/^[ul]/i.test(c) /*/^[lmn]/.test(c)*/) this.skip();
    else if(isIdentifierChar(c) || Lexer.isQuoteChar(c) || /^[.eE]$/.test(c))
      throw this.error(`Invalid number (3): c=${c} ${this.getRange(this.start, this.pos + 1)}`);
    this.addToken(Token.NUMERIC_LITERAL);
    return this.lexText;
  }

  function lexRegExp() {
    let i = 0,
      word = '',
      prev = '',
      slashes = 1,
      bracket = false,
      validator = c => {
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
    const print = () => (word = this.getRange(this.start, this.pos));
    if(this.acceptRun(validator) && slashes == 2) {
      print();
      this.addToken(Token.REGEXP_LITERAL);
      return this.lexText;
    }
    this.backup(this.pos - this.start - 1);
    return this.lexPunctuator();
  }

  function lexPunctuator() {
    let word;
    while(this.accept(Lexer.isPunctuatorChar)) {
      if((word = this.getRange(this.start, this.pos)) != '..' && !isPunctuator(word)) {
        this.backup();
        this.addToken(Token.PUNCTUATOR);
        return this.lexText;
      }
    }
    if(isPunctuator((word = this.getRange(this.start, this.pos)))) {
      this.addToken(Token.PUNCTUATOR);
      return this.lexText;
    }
    throw this.error(`Invalid punctuator: ${word}`);
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
    let c,
      prevChar = this.peek(),
      startToken = this.tokenIndex;
    function template() {
      let pred = Predicate.not(Predicate.or(c => c === '$', Predicate.regexp('^[$`{]'), Predicate.charset('\\`'))
        ),
        escapeEncountered = false,
        n = 0;
      while(!this.eof) {
        if(this.acceptRun(pred)) escapeEncountered = false;
        prevChar = c;
        c = this.getc();
        ++n;
        if(c === null) {
          throw this.error(`Illegal template token (${n})  '${this.source[this.start]}'`);
        } else if(!escapeEncountered) {
          if(c == '{' && prevChar == '$') {
            this.backup(2);
            this.addToken(Token.TEMPLATE_LITERAL);
            this.pos = this.start + 2; //(2);
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
      }
    }
    return template.call(this);
  }

  function lexIdentifier() {
    let c, firstChar, word;
    this.acceptRun(isIdentifierChar);
    if(Lexer.isDecimalDigit((firstChar = this.getRange(this.start, this.start + 1))))
      throw this.error(`Invalid IDENTIFIER`);

    if((c = this.peek()) == '`') {
      const { pos, start } = this;
      this.addToken(Token.IDENTIFIER);
      return this.lexText;
    }
    if(Lexer.isQuoteChar(c)) throw this.error(`Invalid IDENTIFIER`);
    if((word = this.getRange(this.start, this.pos)) === 'true' || word === 'false')
      this.addToken(Token.BOOLEAN_LITERAL);
    else if(word === 'null') this.addToken(Token.NULL_LITERAL);
    else if(isKeyword(word)) this.addToken(Token.KEYWORD);
    else this.addToken(Token.IDENTIFIER);
    return this.lexText;
  }

  function lexQuote() {
    let quoteChar = this.getc(),
      prevChar = '',
      c = '',
      escapeEncountered = false,
      pred;
    if(quoteChar === '`') {
      const { inSubst } = this;
      return this.lexTemplate(inSubst);
    }
    pred = Predicate.and(Predicate.not(Lexer.isLineTerminator),
      Predicate.not(c => c=='\\'|| c == quoteChar)
      //Predicate.regexp(`^[^\\${quoteChar}]`, 'g')
    );
    do {
      if(this.acceptRun(pred)) escapeEncountered = false;
      prevChar = c;
      c = this.getc();

      
      if(c === null) {
        throw this.error(`Illegal token(1)`);
      } else if(!escapeEncountered) {
        if(Lexer.isLineTerminator(c) && quoteChar !== '`') {
          throw this.error(`Illegal token(2) c=${c.codePointAt(0)} quoteChar=${quoteChar} range=${this.getRange()}`);
        } else if(c === quoteChar) {
          this.addToken(Token.STRING_LITERAL);
          return this.lexText;
        } else if(c === '\\') {
          escapeEncountered = true;
        }
      } else {
        escapeEncountered = false;  
      }
    } while(!this.eof);
  }

  function lexSingleLineComment() {
    this.acceptRun(Predicate.not(Lexer.isLineTerminator));
    this.addToken(Token.COMMENT);
    return this.lexText;
  }

  function lexMultiLineComment() {
    do {
      const nextTwo = this.getRange(this.pos, this.pos + 2);
      if(nextTwo === '*/') {
        this.skip(2);
        this.addToken(Token.COMMENT);
        return this.lexText;
      }
      this.getc();
    } while(!this.eof);
  }

  lexer.lexText = lexText;
  lexer.lexQuote = lexQuote;
  lexer.lexNumber = lexNumber;
  lexer.lexRegExp = lexRegExp;
  lexer.lexPunctuator = lexPunctuator;
  lexer.lexIdentifier = lexIdentifier;
  lexer.lexTemplate = lexTemplate;
  lexer.lexSingleLineComment = lexSingleLineComment;
  lexer.lexMultiLineComment = lexMultiLineComment;

  lexer.stateFn = lexText;

  let data;

 
  for(let data of lexer) {
    console.log((data.loc+'').padEnd(16), data.type.padEnd(20), data.toString());

    if(data == null) {
      break;
    }
  }

  std.gc();
}

main(...scriptArgs.slice(1));
