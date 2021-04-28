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
import Parser, { DumpToken } from '../lib/parser.js';
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

/*Token.prototype.inspect = function(options = {}) {
  const { byteLength,start,length,offset,lexeme,loc} = this;
  return inspect({ byteLength, start, length,offset,lexeme,loc});
}
*/
class EBNFParser extends Parser {
  constructor() {
    super(new BNFLexer());

    console.log('lexer.states', this.lexer.states);
    console.log('lexer.tokens', this.lexer.tokens);

    this.lexer.handler = (arg, tok) =>
      console.log(`Unmatched token at ${arg.loc} section=${this.section} state=${
          arg.states[arg.state]
        }\n${arg.currentLine()}\n${' '.repeat(arg.loc.column - 1)}^`
      );

    this.section = 0;
    this.grammar = {
      productions: [],
      lexer: { definitions: [], rules: [] }
    };
  }

  setInput(source, file) {
    const { lexer } = this;
    this.extname = path.extname(file);
    console.log('extname', this.extname);
    return lexer.setInput(source, file);
  }

  addToken(token) {
    const { rules } = this.grammar.lexer;

    if(rules.find(rule => rule.token == token)) return;

    rules.push({ token });
  }
  addDefinition(name, expr) {
    const { definitions } = this.grammar.lexer;

    if(definitions.find(def => def.name == name)) return;

    definitions.push({ name, expr });
  }

  parseDirective() {
    const { directive, newline, identifier } = this.tokens;
    let tok,
      d = this.expect(directive).lexeme.slice(1);
    while(+(tok = this.next()) != newline) {
      DumpToken('parseDirective'.padEnd(20), tok);
      switch (d) {
        case 'token': {
          this.addToken(tok.lexeme);
          break;
        }
        default: {
          break;
        }
      }
      this.consume();
      if(tok.type == 'newline') break;
    }
    //this.expect(newline);
  }

  parseProduction() {
    let id = this.expect('identifier');
    let toks = [];
    this.expect('colon');
    while(!this.match('semi')) toks.push(this.expect(tok => true));
    this.expect('semi');

    toks.forEach(tok => {
      if(tok.type == 'literal') this.addToken(tok.lexeme.slice(1, -1));
    });

    console.log(id.lexeme + ':');
    console.log(toks.map(t => `\t${t.type.padEnd(20)} ${t.lexeme}`).join('\n'));
  }

  parseDefinition() {
    const { l_pattern, l_newline, identifier, l_identifier } = this.tokens;
    this.lexer.pushState('LEXDEFINE');

    let tok,
      name = this.expect(['identifier', 'l_identifier']).lexeme;
    // console.log('parseDefinition', this.lexer.stateStack);

    tok = this.next();
    DumpToken('parseDefinition'.padEnd(20), tok);

    this.consume();

    //  this.expect(l_newline);
  }

  parsePattern() {
    const { lexer } = this;

    let tok;
    while((tok = this.consume())) {
      DumpToken('parsePattern'.padEnd(20), tok);

      if(tok.type.endsWith('newline')) {
        if(lexer.topState() != 'LEXPATTERN') lexer.popState();
        break;
      }
    }
  }

  parse() {
    const {
      lexer,
      extname,
      tokens: { directive, section, identifier, l_identifier, cstart, cend }
    } = this;

    for(;;) {
      let tok = this.next();

      if(tok.type.endsWith('newline')) {
        this.consume();
        continue;
      }

      if(lexer.topState() == 'LEXPATTERN') {
        if(tok.type == 'p_section') {
          lexer.popState();
        } else this.parsePattern();
        continue;
      }
      DumpToken('parse'.padEnd(20), tok);

      switch (tok.type) {
        case 'directive': {
          this.parseDirective();
          break;
        }
        case 'p_section':
        case 'section': {
          this.section++;
          this.consume();

          if(extname == '.y' && this.section == 2) return this.grammar;
          if(extname == '.l') {
            if(this.section == 2) {
              return this.grammar;
            }
            if(this.section == 1) {
              this.lexer.pushState('LEXPATTERN');
              this.expect('p_newline');
            }
          }
          break;
        }
        case 'identifier': {
          if(extname == '.y' && this.section == 1) this.parseProduction();
          if(extname == '.l' && this.section == 0) this.parseDefinition();
          break;
        }
        case 'cstart': {
          while((tok = this.consume())) {
            console.log('tok', tok);

            if(tok.type == 'cend') break;
          }
          break;
        }
        case 'l_newline': {
          this.consume();
          break;
        }
        case -1: {
          return this.grammar;
        }
      }
    }
  }
}

async function main(...args) {
  new Console({
    colors: true,
    depth: 8,
    maxArrayLength: Infinity,
    maxStringLength: Infinity,
    compact: 1,
    showHidden: false,
    customInspect: true
  });

  let file = args[0] ?? 'tests/ANSI-C-grammar-2011.y';
  console.log('file:', file);
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
