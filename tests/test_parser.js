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

function WriteFile(file, str) {
  let f = std.open(file, 'w+');
  f.puts(str);
  let pos = f.tell();
  console.log('Wrote "' + file + '": ' + pos + ' bytes');
  f.close();
  return pos;
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({ start, pos, size })}`;
}

function TokenSeq(tokens) {
  return tokens.map(tok => tok.lexeme);
}

function ConcatPattern(tokens) {
  let out = '';
  for(let tok of tokens) {
    let str = tok.lexeme;
    if(tok.type.endsWith('literal')) {
      str = Lexer.escape(str.slice(1, -1));
      if(/^[A-Za-z0-9_]+$/.test(str)) str += '\\b';
    }
    out += str;
  }
  return out;
}

function TokenName(act) {
  let idx, tok;
  if((idx = act.findIndex(tok => tok.type == 'return')) != -1) {
    act = act.slice(idx);
    while((tok = act.pop())) {
      if(tok.type.endsWith('identifier')) return tok.lexeme;
      if(tok.type.endsWith('literal')) return tok.lexeme.slice(1, -1);
    }
  }
}

function TokenToString(tok) {
  let str = tok.lexeme;
  if(tok.type.endsWith('literal')) str = str.slice(1, -1);
  return str;
}

function InstanceOf(obj, ctor) {
  return typeof obj == 'object' && obj != null && obj instanceof ctor;
}
function IsRegExp(regexp) {
  return InstanceOf(regexp, RegExp);
}

function RegExpToArray(regexp) {
  //console.log("RegExpToArray", regexp);
  const { source, flags } = regexp;
  return [Lexer.unescape(source), flags];
}

function RegExpCompare(r1, r2) {
  //console.log("RegExpCompare", r1,r2);
  [r1, r2] = [r1, r2].map(RegExpToString);
  return r1 === undefined ? -1 : r2 === undefined ? 1 : r1.localeCompare(r2);
}

function RegExpToString(regexp) {
  if(typeof regexp == 'string') return regexp;
  if(IsRegExp(regexp)) return RegExpToArray(regexp)[0];
}

function LoadScript(file) {
  let code = std.loadFile(file);
  //console.log('LoadScript', { code });
  return std.evalScript(code, {});
}

function WriteObject(file, obj, fn = arg => arg) {
  return WriteFile(file,
    fn(
      inspect(obj, {
        colors: false,
        breakLength: 80,
        maxStringLength: Infinity,
        maxArrayLength: Infinity,
        compact: 1
      })
    )
  );
}

function* Range(start, end) {
  for(let i = start | 0; i <= end; i++) yield i;
}

function* MatchAll(regexp, str) {
  let match;
  while((match = regexp.exec(str))) yield match;
}

function SubstDefines(str, definitions) {
  //console.log('SubstDefines', { str, definitions });
  let pos = 0;
  let out = '';
  for(let match of MatchAll(/{([A-Za-z0-9_]+)}/g, str)) {
    const { index, 0: raw, 1: name } = match;
    if(index > pos) out += str.slice(pos, index);
    if(!(name in definitions)) throw new Error(`No definition for ${raw}`);
    out += definitions[name];
    pos = index + raw.length;
    //console.log('match', match, { index, raw, name });
  }
  out += str.slice(pos, str.length);
  console.log('SubstDefines', { str, out });
  return out;
}

function TryCatch(fn) {
  let success = [],
    error = [];

  let ret = function(...args) {
    let ret;
    try {
      ret = fn.call(this, ...args);
    } catch(e) {
      console.log('TryCatch ERROR', e.message);
      for(let handler of error) ret = handler(e);
      return ret;
    } finally {
      // console.log('TryCatch SUCCESS', ret);
      for(let handler of success) ret = handler(ret);
      return ret;
    }
  };
  ret['then'] = handler => (success.push(handler), ret);
  ret['catch'] = handler => (error.push(handler), ret);
  return ret;
}

/*Token.prototype.inspect = function(options = {}) {
  const { byteLength,start,length,offset,lexeme,loc} = this;
  return inspect({ byteLength, start, length,offset,lexeme,loc});
}
*/
class EBNFParser extends Parser {
  constructor(grammar) {
    super(new BNFLexer());

    console.log('lexer.states', this.lexer.states);
    console.log('lexer.tokens', this.lexer.tokens);

    this.lexer.handler = (arg, tok) =>
      console.log(`Unmatched token at ${arg.loc} section=${this.section} state=${
          arg.states[arg.state]
        }\n${arg.currentLine()}\n${' '.repeat(arg.loc.column - 1)}^`
      );

    this.section = 0;
    this.grammar = grammar ?? {
      productions: [],
      lexer: { definitions: {}, rules: [] }
    };
  }

  setInput(source, file) {
    const { lexer } = this;
    this.extname = path.extname(file);
    console.log('extname', this.extname);
    return lexer.setInput(source, file);
  }

  findRule(token, regexp) {
    const { rules } = this.grammar.lexer;
    let rule = rules.find((rule, i) => {
      const re = RegExpToString(rule.regexp);

      if(token === undefined) console.log(`findRule[${i}]`, { regexp, re });

      return typeof token == 'string'
        ? rule.token == token
        : 0 === RegExpCompare(regexp, rule.regexp);
    });
    if(token === undefined) console.log('findRule', { token, regexp, rule });
    return rule;
  }

  addToken(token, regexp) {
    console.log('addToken', { token, regexp });
    const { rules, definitions } = this.grammar.lexer;
    let rule,
      add = r => rules.push(r);
    if((regexp = RegExpToString(regexp))) regexp = SubstDefines(regexp, definitions);

    if((rule = this.findRule(token, regexp))) add = () => {};

    rule ??= {};
    if(typeof token == 'string') rule.token = token;
    if(regexp) rule.regexp = new RegExp(regexp);
    /*TryCatch(() => (rule.regexp = new RegExp(regexp))).catch(error => console.log('ERROR: regexp =', regexp)
      )();*/

    if(['token', 'regexp'].some(prop => prop in rule)) {
      add(rule);
      return rule;
    }
  }

  addDefinition(name, expr) {
    const { definitions } = this.grammar.lexer;
    console.log('addDefinition', { name, expr });

    if('name' in definitions) return;

    definitions[name] = expr;
  }

  parseDirective() {
    const { directive, newline, identifier } = this.tokens;
    let tok,
      d = this.expect(directive).lexeme.slice(1);
    while(+(tok = this.next()) != newline) {
      // DumpToken('parseDirective'.padEnd(20), tok);
      switch (d) {
        case 'token': {
          let rule = this.findRule(tok.lexeme);
          if(!rule) throw new Error(`No such token ${tok.lexeme}`);
          break;
        }
        default: {
          break;
        }
      }
      this.consume();
      if(tok.type == 'newline') break;
    }
  }

  addProduction(symbol, rhs) {
    const { productions } = this.grammar;
    let production = { symbol, rhs };
    productions.push(production);
    return production;
  }

  parseProduction() {
    let id = this.expect('identifier');
    let tok;
    let tokens = [];
    let results = [];
    this.expect('colon');
    const add = () => {
      if(tokens.length) {
        let rhs = [...tokens].map(TokenToString);
        results.push(rhs);
        tokens.clear();
      }
    };

    while((tok = this.consume())) {
      if(tok.type.endsWith('semi')) break;
      if(tok.type == 'bar') {
        add();
        continue;
      }

      tokens.push(tok);
    }

    add();
    //   this.expect('semi');

    tokens.forEach(tok => {
      if(tok.type == 'literal') this.addToken(tok.lexeme.slice(1, -1));
    });

    console.log(id.lexeme + ':', results);

    for(let result of results) {
      this.addProduction(id.lexeme, result);
    }
    //   console.log(tokens.map(t => `\t${t.type.padEnd(20)} ${t.lexeme}`).join('\n'));
    //     console.log('results', ));
  }

  parseDefinition() {
    const { l_pattern, l_newline, identifier, l_identifier } = this.tokens;
    this.lexer.pushState('LEXDEFINE');

    let expr,
      name = this.expect(['identifier', 'l_identifier']).lexeme;
    // console.log('parseDefinition', this.lexer.stateStack);

    expr = this.next();
    //DumpToken('parseDefinition'.padEnd(20), expr);

    this.addDefinition(name, expr.lexeme);

    this.consume();

    //  this.expect(l_newline);
  }

  parsePattern() {
    const { lexer } = this;
    let tok,
      pat = [],
      act = [];
    let arr = pat;
    while((tok = this.consume())) {
      //DumpToken('parsePattern'.padEnd(20), tok);
      if(tok.type.endsWith('cstart')) arr = act;
      if(tok.type.endsWith('newline')) {
        if(lexer.topState() != 'LEXPATTERN') lexer.popState();
        break;
      }
      arr.push(tok);
    }
    let ret;
    if(!(ret = this.addToken(TokenName(act), '^' + ConcatPattern(pat))))
      console.log('parsePattern', {
        pat: TokenSeq(pat),
        act: TokenSeq(act),
        expr: ConcatPattern(pat)
      });
    console.log('parsePattern', ret);
    return ret;
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
  /*console.log('newline', '\n');

  function TestRegExp(char) {
    let re;
    TryCatch(() => (re = new RegExp(char))).catch(err => (re = new RegExp((char = Lexer.escape(char))))
    )();
    let source = re ? RegExpToString(re) : undefined;
    if(char != source) throw new Error(`'${char}' != '${source}'`);
    console.log({ char, re, source, ok: char == source });
  }

  [...Range(0, 127)].map(code => {
    let char = String.fromCharCode(code);
    TestRegExp(char);
  });
  TestRegExp('\b');
  TestRegExp('\\b');*/

  let file = args[0] ?? 'tests/ANSI-C-grammar-2011.y';
  let outputFile = args[1] ?? 'grammar.kison';
  console.log('file:', file);
  let str = std.loadFile(file, 'utf-8');
  let len = str.length;
  let type = path.extname(file).substring(1);

  let grammar = LoadScript(outputFile);
  //  console.log('grammar:', grammar);

  let parser = new EBNFParser(grammar);

  parser.setInput(str, file);

  grammar = parser.parse();

  WriteObject('grammar.kison',
    grammar,
    str => `(function () {\n    return ${str.replace(/\n/g, '\n    ')};\n\n})();`
  );
  console.log('grammar:', grammar);

  std.gc();
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
