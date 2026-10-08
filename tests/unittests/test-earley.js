/* lib/parser/{earley,cfg,scan}.js: grammars run against input, as utilities/parse-ast.js does. */
import { Earley } from '../../lib/parser/earley.js';
import { toCFG } from '../../lib/parser/cfg.js';
import { charItems, g4Lexer, lexItems } from '../../lib/parser/scan.js';
import { parseBNF } from '../../lib/parser/bnf.js';
import { parseEBNF } from '../../lib/parser/ebnf.js';
import { parseG4 } from '../../lib/parser/g4.js';
import { parseYacc } from '../../lib/parser/yacc.js';
import { parseLex } from '../../lib/parser/lex.js';
import { eq, tests } from '../../lib/tinytest.js';

const T = c => ({ match: t => t == c, name: c });

/* a tree as s-expression: (label-or-rule children...), tokens by text */
const show = n => (n.token ? n.token.text ?? n.token : `(${n.label ?? n.rule}${n.children.map(c => ' ' + show(c)).join('')})`);

const run = (grammar, kind, items, opts) => {
  const cfg = toCFG(grammar, kind, opts);

  return show(new Earley(cfg).parse(items));
};

tests({
  'earley: precedence levels and associativity pick the tree'() {
    const e = new Earley({
      start: 'e',
      rules: {
        e: [
          { rhs: ['e', T('+'), 'e'], prec: 1, assoc: 'left', label: 'add' },
          { rhs: ['e', T('*'), 'e'], prec: 2, assoc: 'left', label: 'mul' },
          { rhs: [T('n')] },
        ],
      },
    });
    const p = s => show(e.parse([...s]));

    eq('(add (e n) + (mul (e n) * (e n)))', p('n+n*n'));
    eq('(add (add (e n) + (e n)) + (e n))', p('n+n+n'));
  },

  'earley: empty rules and a helper repeat'() {
    const e = new Earley({ start: 's', rules: { s: [{ rhs: ['$r', T('x')] }], $r: [{ rhs: [] }, { rhs: ['$r', T('a')] }] } });

    eq('(s a a x)', show(e.parse([...'aax'])));
    eq('(s x)', show(e.parse(['x'])));
  },

  'earley: a failure names the item and what was expected'() {
    const e = new Earley({ start: 's', rules: { s: [{ rhs: [T('a'), T('b')] }] } });

    try {
      e.parse([...'ac']);
    } catch(x) {
      eq(1, x.index);
      eq('b', x.expected.join());
      return;
    }
    eq(true, false);
  },

  'bnf: characters, with whitespace skipped'() {
    const g = parseBNF('<e> ::= <e> "+" <n> | <n>\n<n> ::= "1" | "2"');

    eq('(e (e (n 1)) + (n 2))', run(g, 'bnf', charItems('1 + 2', true), { scannerless: true }));
  },

  'ebnf: repeats, classes and an exception'() {
    const g = parseEBNF('word ::= [a-z]+\nany ::= [a-z] - "q"');

    eq('(word a b c)', run(g, 'ebnf', charItems('abc'), { scannerless: true }));
    eq('(any x)', run(g, 'ebnf', charItems('x'), { scannerless: true, start: 'any' }));
  },

  'g4: lexer rules, skip, implicit literals and left recursion'() {
    const g = parseG4(`grammar E;
e : e '*' e | e '+' e | INT ;
INT : [0-9]+ ;
WS : [ ]+ -> skip ;`);
    const cfg = toCFG(g, 'g4');
    const items = g4Lexer([g], cfg.literals)('1 + 2 * 3');

    eq('INT,+,INT,*,INT', items.map(i => i.type).join());
    eq('(e (e 1) + (e (e 2) * (e 3)))', show(new Earley(cfg).parse(items)));
  },

  'g4: modes and the longest match'() {
    const g = parseG4(`lexer grammar L;
A : 'a' ;
AB : 'ab' ;
OPEN : '<' -> pushMode(IN) ;
mode IN;
X : 'x' ;
CLOSE : '>' -> popMode ;`);

    eq('AB,A,OPEN,X,CLOSE,A', g4Lexer([g])('aba<x>a').map(i => i.type).join());
  },

  'g4 lexer: recursive rule (balanced =)'() {
    const g = parseG4(`lexer grammar L;
LONG : '[' NEST ']' ;
fragment NEST : '=' NEST '=' | '[' .*? ']' ;
ID : [a-z]+ ;
WS : ' ' -> skip ;`);

    eq('ID,LONG,ID', g4Lexer([g])('a [==[ x ]] y ]==] b').map(i => i.type).join());
    eq('[=[]]=]', g4Lexer([g])('[=[]]=]')[0].text);
  },

  'yacc: precedence, %start and a lex file'() {
    const y = parseYacc(`%token NUM
%left '+'
%left '*'
%start e
%%
e : e '+' e | e '*' e | NUM ;
`);
    const l = parseLex(`%%\n[0-9]+\t{ return NUM; }\n"+"\t{ return '+'; }\n"*"\t{ return '*'; }\n[ ]+\t;\n`);
    const items = lexItems(l, '1 + 2 * 3');

    eq('NUM,+,NUM,*,NUM', items.map(i => i.type).join());
    eq('(e (e 1) + (e (e 2) * (e 3)))', show(new Earley(toCFG(y, 'yacc')).parse(items)));
  },

  'yacc: a rule that returns a call gets the identifier token'() {
    const l = parseLex('%%\n[a-z]+\t{ return check(); }\n[ ]+\t;\n');

    eq('NAME,NAME', lexItems(l, 'ab cd', { identToken: 'NAME' }).map(i => i.type).join());
  },
});
