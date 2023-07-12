import BNFLexer from '../lexer/bnf.js';
import { DumpToken } from '../parser.js';
import Parser from '../parser.js';
function TokenSeq(tokens) {
  return tokens.map(tok => tok.lexeme);
}

function ConcatPattern(tokens) {
  let out = '';
  for(let tok of tokens) {
    let str = tok.lexeme;
    if(tok.type.endsWith('literal')) {
      str = BNFLexer.escape(str.slice(1, -1));
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

function RegExpCompare(r1, r2) {
  //console.log("RegExpCompare", r1,r2);
  [r1, r2] = [r1, r2].map(RegExpToString);
  return r1 === undefined ? -1 : r2 === undefined ? 1 : r1.localeCompare(r2);
}

function RegExpToString(regexp) {
  if(typeof regexp == 'string') return regexp;
  if(IsRegExp(regexp)) return RegExpToArray(regexp)[0];
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

export function TryCatch(fn) {
  let success = [],
    error = [];

  let ret = function(...args) {
    let ret;
    try {
      ret = fn.call(this, ...args);
    } catch(e) {
      console.log('TryCatch ERROR:', e.message);
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

function MatchString(str) {
  return tok => tok.lexeme.trim() == str;
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
      console.log('Balancer', {
        tok: InspectToken(tok),
        stack: stack.map(InspectToken)
      });
    return stack.length == 0;
  };
}

function Node(token) {
  let obj = this ?? new Node();
  if(token) {
    const { type, lexeme } = token;
    let ctor = Node;
    switch (type) {
      case 'identifier':
        ctor = NonTerminal;
        break;
      case 'char_class':
      case 'literal':
        ctor = Terminal;
        break;
      default:
        throw new Error(`Unhandled token type: ${type}`);
        break;
    }
    if(ctor !== Node) obj = new ctor(lexeme);
  }
  return obj;
}

Object.defineProperties(Node.prototype, {
  type: {
    get() {
      return this.constructor.name;
    }
  },
  toJSON: {
    value() {
      return { type: this.type, ...this };
    }
  }
});
Object.assign(Node, {
  from(arr) {}
});

class Terminal extends Node {
  constructor(value) {
    super();
    this.value = value;
  }
  toString() {
    return this.value;
  }
}

class NonTerminal extends Node {
  constructor(name) {
    super();
    this.name = name;
  }
  toString() {
    return `${this.name}`;
  }
}

class List extends Node {
  constructor(nodes, separator) {
    super();
    this.nodes = nodes;
    this.separator = separator;
  }

  toString(parens_if_multiple) {
    let s = this.nodes.map(node => node.toString()).join(this.separator);
    if(parens_if_multiple && this.nodes.length > 1) s = `(${s})`;
    return s;
  }
}

class Alternatives extends List {
  constructor(nodes) {
    super(nodes, ' | ');
  }
}

class Sequence extends List {
  constructor(nodes) {
    super(nodes, ' ');
  }
}

class Operator extends Node {
  constructor(node, operator) {
    super();
    this.node = node;
    this.operator = operator;
  }
  toString() {
    return this.operator(this.node.toString(true));
  }
}

class Optional extends Operator {
  constructor(node) {
    super(node, s => s + '?');
  }
}

class OneOrMany extends Operator {
  constructor(node) {
    super(node, s => s + '+');
  }
}

class Any extends Operator {
  constructor(node) {
    super(node, s => s + '*');
  }
}

class Not extends Operator {
  constructor(node) {
    super(node, s => '~' + s);
  }
}

class EBNFParser extends Parser {
  constructor(grammar) {
    super(new BNFLexer());

    /* console.log('lexer.states', this.lexer.states);
    console.log('lexer.tokens', this.lexer.tokens);*/

    this.lexer.handler = (arg, tok) => {
      let line = arg.currentLine();
      let index = arg.loc.column - 1;
      let error = new Error(
        `Unmatched token at ${arg.loc} char='${BNFLexer.escape(line[index])}' section=${this.section} state=${arg.states[arg.state]}\n${line}\n${[...line]
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

  findRule(token, regexp) {
    const { rules } = this.grammar.lexer;
    let rule = rules.find((rule, i) => {
      const re = RegExpToString(rule.regexp);
      return typeof token == 'string' ? rule.token == token : 0 === RegExpCompare(regexp, rule.regexp);
    });
    return rule;
  }

  addRule(token, regexp, states) {
    console.log('addRule', { token, regexp, states });
    const { rules, definitions } = this.grammar.lexer;
    let rule,
      add = r => rules.push(r);
    if((regexp = RegExpToString(regexp))) regexp = SubstDefines(regexp, name => this.getDefinition(name));
    rule ??= {};
    if(typeof token == 'string') rule.token = token;
    if(regexp)
      //rule.regexp = new RegExp(regexp);
      TryCatch(() => (rule.regexp = new RegExp(regexp))).catch(error => console.log('ERROR: regexp =', regexp))();
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
    return new Error(`${tok.loc} ${tok.type} '${BNFLexer.escape(tok.lexeme)}' ${message}\n${lexer.currentLine()}\n${' '.repeat(tok.loc.column - 1) + '^'}`);
  }

  parseDirective() {
    const { directive, newline, identifier } = this.tokens;
    let tok,
      d = this.expect(directive).lexeme.slice(1);
    while((tok = this.next())) {
      const { type, lexeme } = tok;
      //console.log('parseDirective',{directive, type, lexeme, d});
      if(type == 'newline') break;
      //DumpToken('parseDirective'.padEnd(20), tok);
      switch (d) {
        case 'token': {
          let rule = this.findRule(tok.lexeme);
          if(!rule) {
            this.grammar.lexer.rules[tok.lexeme] = {};
            //throw this.error(tok, `No such token`);
          }
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
    console.log(`addProduction(${this.lexer.topState()})`.padEnd(20), {
      symbol,
      rhs
    });
    return production;
  }

  parseDefinition() {
    this.lexer.pushState('LEXDEFINE');
    const { l_pattern, l_newline, identifier, l_identifier } = this.tokens;
    let expr,
      name = this.expect(['identifier', 'l_identifier']).lexeme;
    //DumpToken(`parseDefinition(${this.lexer.topState()})`.padEnd(20), this.next());
    let tok = this.next();

    if(tok.type.endsWith('ws')) this.expect(tok.type);
    this.lexer.pushState('LEXPATTERN');
    expr = this.parsePattern();
    this.lexer.popState();
    //DumpToken(`parseDefinition(${this.lexer.topState()})`.padEnd(20), this.next());
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
    const balancer = new Balancer(MatchString('{'), MatchString('}'));
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
      //DumpToken(`parseUntil(${this.lexer.topState()})`.padEnd(20), tok);
      if(!tok.type.endsWith('newline')) arr.push(tok);
      if(endCond(tok)) break;
      this.consume();
    }
    return arr;
  }

  parseRule() {
    //DumpToken(`parseRule(${this.lexer.topState()})`.padEnd(20), this.next());
    const { lexer } = this;
    let tok,
      pat = this.parsePattern(tok => tok.type.endsWith('ws') || tok.type.endsWith('newline') || tok.type.endsWith('cstart')),
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

  parseProduction() {
    let tok;
    let id = (tok = this.expect('identifier'));
    //DumpToken(`parseProduction(${this.lexer.topState()})`.padEnd(20), this.next());
    let tokens = [];
    let action = [];
    let results = [];
    this.expect('colon');
    const add = () => {
      if(tokens.length) {
        results.push([[...tokens], [...action]]);
        tokens.clear();
        action.clear();
      }
    };
    while((tok = this.next())) {
      if(this.lexer.topState() != 'INITIAL') {
        action = this.parseAction();
      } else if(tok.type == 'arrow') {
        this.lexer.pushState(code);
        action = this.parseUntil();
        this.lexer.popState();
        continue;
      } else if(['lbrace', 'cstart', 'arrow'].some(t => tok.type.endsWith(t))) {
        this.lexer.pushState(code);
        continue;
      }

      if(this.extname == '.g4') this.parseTree();

      tok = this.consume();
      if(tok.type == 'bar') {
        add();
        continue;
      }
      if(tok.type.endsWith('semi')) break;
      tokens.push(tok);
    }
    add();
    console.log(
      id.lexeme + ':',
      results.map(([tokens, action]) => {
        let obj = { tokens: tokens.map(InspectToken) };
        if(action.length) obj.action = action.map(InspectToken);
        return obj;
      })
    );
    for(let [tokens, action] of results) {
      let node = new Node(tokens[0]);
      console.log('node:', node.toJSON());
      this.addProduction(id.lexeme, tokens.map(TokenToString), action);
    }
  }
  parseTree(level = 0) {
    let tok, node;
    while((tok = this.next())) {
      DumpToken(`parseTree(${level})`.padEnd(20), tok);
      if(['semi', 'bar'].some(t => tok.type == t)) break;
      if([';', '|'].some(l => tok.lexeme == l)) break;
      tok = this.consume();

      switch (tok.type) {
        case 'literal':
        case 'identifier': {
          node = new Node(tok);
          break;
        }
        case 'tilde': {
          break;
        }

        case 'lparen': {
          let list = [];
          while((tok = this.next())) {
            if(tok.type == 'rparen') {
              this.consume();
              break;
            }

            list.push(this.parseTree(level + 1));
          }
          node = new Sequence(list);

          break;
        }
        default: {
          throw new Error(`parseTree: unhandled token: ${tok.type}`);
          break;
        }
      }
      if(node) {
        while((tok = this.next())) {
          DumpToken(`parseTree(${level})`.padEnd(20), tok);
          let ctor = { question: Optional, plus: OneOrMany, asterisk: Any }[tok.type];

          if(!ctor) break;

          node = new ctor(node);
          this.consume();
        }
      }

      //   break;
    }

    return node;
  }

  parse() {
    const {
      lexer,
      extname,
      tokens: { directive, section, identifier, l_identifier, cstart, cend }
    } = this;
    for(;;) {
      let tok = this.next();
      if(!tok || tok == -1) return this.grammar;
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
      console.log(`parse(${lexer.topState()})`.padEnd(20), lexer.pos, tok);
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
          if(extname == '.g4') {
            if(this.section == 0 && tok.lexeme == 'grammar') {
              while((tok = this.consume())) {
                if(tok.lexeme == ';') break;
              }
              continue;
            }
            if(tok.lexeme == 'fragment') this.consume();
            this.parseProduction();
          } else if(extname == '.y' && this.section == 1) {
            this.parseProduction();
          } else if(extname == '.l' && this.section == 0) {
            this.parseDefinition();
          }
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

export default EBNFParser;
