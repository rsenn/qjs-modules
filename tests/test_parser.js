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

let code = 'C';

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
  let idx, tok, ret;
  if((idx = act.findIndex(tok => ['return', 'BEGIN'].indexOf(tok.lexeme) != -1)) != -1) {
    act = act.slice(idx + 1);
    if(act[0].type.endsWith('paren')) act.shift();
    //console.log("TokenName", act.map(InspectToken));
    while((tok = act.shift())) {
      console.log('TokenName', tok);
      if(tok.type.endsWith('identifier')) {
        ret = tok.lexeme;
        break;
      }
      if(tok.type.endsWith('literal')) {
        ret = tok.lexeme.slice(1, -1);
        break;
      }
      break;
    }
    console.log('TokenName', ret);

    return ret;
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
  let get = typeof definitions == 'function' ? definitions : name => definitions[name];

  for(let match of MatchAll(/{([A-Za-z0-9_]+)}/g, str)) {
    const { index, 0: raw, 1: name } = match;
    if(index > pos) out += str.slice(pos, index);
    let value = get(name);
    if(typeof value != 'string') throw new Error(`No definition for ${name}`);
    if(/^[^\(]*[^\\]\|/.test(value)) value = `(${value})`;
    out += value;
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

function TokenPredicate(char_or_fn) {
  if(typeof char_or_fn == 'function') return char_or_fn;
  return tok => tok.type == char_or_fn || tok.lexeme == char_or_fn;
}
function InspectToken(tok) {
  return [tok.loc + '', tok.type, tok.lexeme];
}

function Balancer(open, close, debug) {
  let is = { open: TokenPredicate(open), close: TokenPredicate(close) };
  let stack = [];
  return function Balancer(tok) {
    const open = is.open(tok);
    const close = is.close(tok);

    if(open) stack.push(tok);
    if(close) stack.pop();

    if(debug && (open || close))
      console.log('Balancer', { tok: InspectToken(tok), stack: stack.map(InspectToken) });
    return stack.length == 0;
  };
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

    this.lexer.handler = (arg, tok) => {
      let line = arg.currentLine();
      let index = arg.loc.column - 1;
      let error = new Error(`Unmatched token at ${arg.loc} char='${Lexer.escape(line[index])}' section=${
          this.section
        } state=${arg.states[arg.state]}\n${line}\n${[...line]
          .slice(0, index)
          .map(c => (c != '\t' ? ' ' : c))
          .join('')}^`
      );
      console.log(error.message);
      console.log('tokens', [...this.processed, ...this.buffer].slice(-10).map(InspectToken));
      throw error;
    };

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

      //if(token === undefined) console.log(`findRule[${i}]`, { regexp, re });

      return typeof token == 'string'
        ? rule.token == token
        : 0 === RegExpCompare(regexp, rule.regexp);
    });
    //if(token === undefined) console.log('findRule', { token, regexp, rule });
    return rule;
  }

  addRule(token, regexp, states) {
    console.log('addRule', { token, regexp, states });
    const { rules, definitions } = this.grammar.lexer;
    let rule,
      add = r => rules.push(r);
    if((regexp = RegExpToString(regexp)))
      regexp = SubstDefines(regexp, name => this.getDefinition(name));

    //if((rule = this.findRule(token, regexp))) add = () => {};

    rule ??= {};
    if(typeof token == 'string') rule.token = token;
    if(regexp)
      //rule.regexp = new RegExp(regexp);
      TryCatch(() => (rule.regexp = new RegExp(regexp))).catch(error =>
        console.log('ERROR: regexp =', regexp)
      )();

    if(states) rule.state = states.length == 1 ? states[0] : states;

    if(['token', 'regexp'].some(prop => prop in rule)) {
      add(rule);
      return rule;
    }
  }

  addDefinition(name, expr) {
    const { grammar } = this;
    if(grammar.lexer == undefined) grammar.lexer = { definitions: {}, rules: [] };

    console.log('addDefinition', { name, expr });

    if(name in grammar.lexer.definitions) return;

    grammar.lexer.definitions[name] = expr;
  }

  getDefinition(name) {
    const { definitions } = this.grammar.lexer;

    let value = definitions[name];

    while(/{[A-Za-z0-9_]+}/.test(value)) {
      let newVal = SubstDefines(value, definitions);
      if(newVal != value) console.log('getDefinition', { newVal, value });
      else break;
      value = newVal;
    }

    return value;
  }

  error(tok, message) {
    const { lexer } = this;
    return new Error(`${tok.loc} ${tok.type} '${Lexer.escape(tok.lexeme)}' ${message}\n${lexer.currentLine()}\n${
        ' '.repeat(tok.loc.column - 1) + '^'
      }`
    );
  }

  parseDirective() {
    const { directive, newline, identifier } = this.tokens;
    let tok,
      d = this.expect(directive).lexeme.slice(1);
    while(+(tok = this.next()) >= 0) {
      if(tok.type == 'newline') break;
      DumpToken('parseDirective'.padEnd(20), tok);
      switch (d) {
        case 'token': {
          let rule = this.findRule(tok.lexeme);
          if(!rule) throw this.error(tok, `No such token`);
          break;
        }
        default: {
          break;
        }
      }
      this.consume();
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
    let action = [];
    let results = [];
    this.expect('colon');
    const add = () => {
      if(tokens.length) {
        let rhs = [...tokens].map(TokenToString);
        results.push(rhs);
        tokens.clear();
      }
    };
    while((tok = this.next())) {
      if(this.lexer.topState() != 'INITIAL') {
        action = this.parseAction();
      } else if(tok.type.endsWith('lbrace') || tok.type.endsWith('cstart')) {
        this.lexer.pushState(code);
      }

      tok = this.consume();

      if(tok.type == 'bar') {
        add();
        continue;
      }
      if(tok.type.endsWith('semi')) break;
      tokens.push(tok);
    }
    add();
    /*  tokens.forEach(tok => {
      if(tok.type == 'literal') this.addRule(tok.lexeme.slice(1, -1));
    });*/
    console.log(id.lexeme + ':', results);
    for(let result of results) {
      this.addProduction(id.lexeme, result);
    }
  }

  parseDefinition() {
    this.lexer.pushState('LEXDEFINE');
    const { l_pattern, l_newline, identifier, l_identifier } = this.tokens;
    let expr,
      name = this.expect(['identifier', 'l_identifier']).lexeme;
    DumpToken(`parseDefinition(${this.lexer.topState()})`.padEnd(20), this.next());
    let tok = this.next();

    if(tok.type.endsWith('ws')) this.expect(tok.type);
    this.lexer.pushState('LEXPATTERN');
    expr = this.parsePattern();
    this.lexer.popState();
    DumpToken(`parseDefinition(${this.lexer.topState()})`.padEnd(20), this.next());
    // console.log(`parseDefinition(${this.lexer.topState()})`.padEnd(20), expr.map(InspectToken));
    this.addDefinition(name, ConcatPattern(expr));
    this.lexer.popState();
  }

  parsePattern(endCond = tok => tok.type.endsWith('newline')) {
    let tok,
      pattern = [];
    if(this.lexer.topState() != 'LEXPATTERN') this.lexer.pushState('LEXPATTERN');
    while((tok = this.next())) {
      if(endCond(tok)) break;
      pattern.push(tok);
      this.consume();
    }
    //  console.log(`parsePattern(${this.lexer.topState()})`.padEnd(20), pattern.map(InspectToken));
    return pattern;
  }

  parseAction() {
    const { lexer } = this;
    let tok,
      act = [];
    const balancer = new Balancer(tok => tok.lexeme.trim() == '{',
      tok => tok.lexeme.trim() == '}'
    );

    if(lexer.topState() != code) lexer.pushState(code);

    while((tok = this.consume())) {
      if(!tok.type.endsWith('newline')) act.push(tok);
      if(balancer(tok)) {
        if(lexer.topState() == code) lexer.popState();
        break;
      }
    }
    return act;
  }

  parseUntil(endCond = tok => tok.type.endsWith('newline')) {
    let tok,
      arr = [];
    while((tok = this.next())) {
      DumpToken(`parseUntil(${this.lexer.topState()})`.padEnd(20), tok);
      if(!tok.type.endsWith('newline')) arr.push(tok);
      if(endCond(tok)) break;
      this.consume();
    }
    return arr;
  }

  parseRule() {
    DumpToken(`parseRule(${this.lexer.topState()})`.padEnd(20), this.next());
    const { lexer } = this;
    let tok,
      pat = this.parsePattern(tok =>
          tok.type.endsWith('ws') || tok.type.endsWith('newline') || tok.type.endsWith('cstart')
      ),
      act = [];
    tok = this.next();
    if(tok.type.endsWith('cstart')) act = this.parseAction();
    else {
      this.consume();
      lexer.pushState(code);
      act = this.parseUntil();
      lexer.popState();
    }
    console.log(`parseRule(${this.lexer.topState()})`.padEnd(20), {
      pat: pat.map(InspectToken),
      act: act.map(InspectToken)
    });
    let ret;
    let pattern = ConcatPattern(pat);
    let match = /^(<[^>]*>|)/.exec(pattern);
    let states = match[0].length ? match[0].slice(1, -1).split(',') : null;
    if(!(ret = this.addRule(TokenName(act), '^' + pattern.slice(match[0].length), states)))
      console.log(`parseRule(${this.lexer.topState()})`.padEnd(20), {
        pat: TokenSeq(pat),
        act: TokenSeq(act),
        expr: ConcatPattern(pat)
      });
    console.log(`parseRule(${this.lexer.topState()})`.padEnd(20), ret);
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

      if(!tok) break;

      if((tok.type + '').endsWith('newline')) {
        this.consume();
        continue;
      }

      if(lexer.topState() == 'LEXPATTERN') {
        if(tok.type == 'p_section') {
          lexer.popState();
        } else this.parseRule();
        continue;
      }
      if(lexer.topState() == 'LEXDEFINE' && tok.type == 'l_identifier') {
        this.parseDefinition();
        continue;
      }
      DumpToken(`parse(${lexer.topState()})`.padEnd(20), tok);

      switch (tok.type) {
        case 'directive': {
          this.parseDirective();
          break;
        }
        case 'l_section':
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
            console.log('tok', InspectToken(tok));

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
  /*console =*/ new Console({
    colors: true,
    depth: Infinity,
    maxArrayLength: Infinity,
    maxStringLength: Infinity,
    breakLength: 80,
    compact: 1,
    showHidden: false,
    customInspect: true
  });
  console.log('console.options', console.options);
  let optind = 0;
  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) {
      code = globalThis.code = args[++optind].toUpperCase();
    }

    optind++;
  } /*
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

  let file = args[optind] ?? 'tests/ANSI-C-grammar-2011.y';
  let outputFile = args[optind + 1] ?? 'grammar.kison';
  console.log('file:', file);
  let str = std.loadFile(file, 'utf-8');
  let len = str.length;
  let type = path.extname(file).substring(1);

  let grammar = LoadScript(outputFile);
  //  console.log('grammar:', grammar);

  let parser = new EBNFParser(grammar);

  parser.setInput(str, file);

  grammar = parser.parse();
  if(grammar) {
    WriteObject('grammar.kison',
      grammar,
      str => `(function () {\n    return ${str.replace(/\n/g, '\n    ')};\n\n})();`
    );
    console.log('grammar:', grammar);
  }
  std.gc();
  return !!grammar;
}

main(...scriptArgs.slice(1))
  .then(ret => console.log(ret ? 'SUCCESS' : 'FAIL'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
111;