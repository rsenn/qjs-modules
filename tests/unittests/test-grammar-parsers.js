/* lib/parser/{bnf,ebnf,g4,yacc}.js: grammar files into an AST.
 * The last test reads ~/Sources/plot-cv/lib/grammars when it exists. */
import { existsSync, readdirSync, readFileSync } from 'fs';
import { parseBNF } from '../../lib/parser/bnf.js';
import { parseEBNF } from '../../lib/parser/ebnf.js';
import { parseG4 } from '../../lib/parser/g4.js';
import { parseYacc } from '../../lib/parser/yacc.js';
import { parseLex } from '../../lib/parser/lex.js';
import { eq, tests } from '../../lib/tinytest.js';

/* a Sequence's items as 'Type:name' strings, for short comparisons */
const short = n => (n.type == 'Terminal' ? `'${n.value}'` : n.type == 'Nonterminal' ? n.name : n.type);
const alts = rule => rule.body.alternatives.map(a => a.items.map(short).join(' '));

tests({
  'bnf: rules, alternatives, empty alternative and terminals'() {
    const g = parseBNF('<expr> ::= <term> | "+" <expr>\n<term> ::= \'x\' |\nfoo ::= <expr> ;');

    eq('expr,term,foo', g.rules.map(r => r.name).join());
    eq(`term|'+' expr`, alts(g.rules[0]).join('|'));
    eq(`'x'|`, alts(g.rules[1]).join('|'));
  },

  'bnf: a missing terminator ends at the next rule'() {
    eq(2, parseBNF('a ::= b c\nd ::= e').rules.length);
  },

  'bnf: an error names file, line and column'() {
    try {
      parseBNF('a ::= b )', 'x.bnf');
    } catch(e) {
      eq(true, /^x\.bnf:1:9:/.test(e.message));
      return;
    }
    eq(true, false);
  },

  'ebnf: groups, repeats, options and the postfix operators'() {
    const g = parseEBNF('expr ::= term (("+"|"-") term)* [x] #x41 [a-z]+ b - c');
    const items = g.rules[0].body.alternatives[0].items;

    eq('Nonterminal,Repeat,CharClass,Char,Repeat,Except', items.map(i => i.type).join());
    eq('*', items[1].op);
    eq(65, items[3].code);
    eq('+', items[4].op);
  },

  'ebnf: ISO names with spaces, `,`, { } and [ ]'() {
    const g = parseEBNF('decimal digit = "0" | "1";\nnumber = digit, { digit }, [ ? sign ? ];');

    eq('decimal digit,number', g.rules.map(r => r.name).join());
    eq('Nonterminal,Repeat,Optional', g.rules[1].body.alternatives[0].items.map(i => i.type).join());
    eq('sign', g.rules[1].body.alternatives[0].items[2].body.alternatives[0].items[0].text);
  },

  'ebnf: (* *) comments and a count before *'() {
    const g = parseEBNF('a = (* c *) 3 * "x";');

    eq(3, g.rules[0].body.alternatives[0].items[0].count);
  },

  'g4: header, options, tokens, a rule with labels and a lexer command'() {
    const g = parseG4(`lexer grammar L;
options { superClass = Base; }
tokens { A, B }
@members { int x; }
fragment D : [0-9] ;
ID : [a-z]+ -> channel(HIDDEN) ;
WS : ' ' -> skip ;`);

    eq('lexer', g.kind);
    eq('L', g.name);
    eq('Options,Tokens,ActionDecl', g.decls.map(d => d.type).join());
    eq('superClass', g.decls[0].items[0].name);
    eq('A,B', g.decls[1].names.join());
    eq('D,ID,WS', g.rules.map(r => r.name).join());
    eq(true, g.rules[0].fragment);
    eq('channel', g.rules[1].body.alternatives[0].commands[0].name);
    eq('HIDDEN', g.rules[1].body.alternatives[0].commands[0].arg);
  },

  'g4: labels, # alternative labels, non-greedy, predicates, ranges, not-sets'() {
    const g = parseG4(`grammar P;
e : l=e '*' r=e # Mul
  | {p()}? ID
  | ~[a-z] .*? 'a'..'z'
  | x+=ID
  ;`);
    const [mul, pred, other, plus] = g.rules[0].body.alternatives;

    eq('Mul', mul.label);
    eq('Label,Terminal,Label', mul.items.map(i => i.type).join());
    eq('Predicate,Nonterminal', pred.items.map(i => i.type).join());
    eq('Not,Repeat,Range', other.items.map(i => i.type).join());
    eq(false, other.items[1].greedy);
    eq('+=', plus.items[0].op);
  },

  'g4: mode, rule arguments and returns'() {
    const g = parseG4('lexer grammar M;\nmode X;\nA : \'a\' ;\nr[int n] returns [int v] : A ;');

    eq('Mode,Rule,Rule', g.rules.map(r => r.type).join());
    eq('X', g.rules[0].name);
    eq('[int n]', g.rules[2].args);
    eq('[int v]', g.rules[2].returns);
  },

  'yacc: declarations, rules with actions, %prec and the epilogue'() {
    const g = parseYacc(`%{
#include <x.h>
%}
%token A B
%left '+'
%%
e : e '+' e %prec UP { $$ = $1; }
  | A
  ;
%%
int main() {}
`);

    eq('Prologue,Directive,Directive', g.decls.map(d => d.type).join());
    eq('%token', g.decls[1].name);
    eq('A,B', g.decls[1].args.map(a => a.text).join());
    eq('e', g.rules[0].name);
    eq('Nonterminal,Terminal,Nonterminal,Directive,Action', g.rules[0].body.alternatives[0].items.map(i => i.type).join());
    eq('%prec', g.rules[0].body.alternatives[0].items[3].name);
    eq('\nint main() {}\n', g.epilogue);
  },

  'yacc: [alias], a rule without `;`, jison %lex block and EBNF postfix'() {
    const g = parseYacc(`%lex
%%
\\s+ /* skip */
/lex
%%
a : b[left] c* | d
b : 'x' ;
`);

    eq('Lex', g.decls[0].type);
    eq('a,b', g.rules.map(r => r.name).join());
    eq('left', g.rules[0].body.alternatives[0].items[0].alias);
    eq('Repeat', g.rules[0].body.alternatives[0].items[1].type);
    eq(parseLex(g.decls[0].text).rules.length, 1);
  },

  'corpus: every grammar file of plot-cv parses'() {
    const dir = process.env.HOME + '/Sources/plot-cv/lib/grammars';

    if(!existsSync(dir)) return;

    const files = [];
    const walk = d => {
      for(const name of readdirSync(d)) {
        const p = d + '/' + name;

        if(/\.(g4|y|jison|ebnf|l|jisonlex)$/.test(name)) files.push(p);
        else if(name == 'Java' || name == 'JavaScript') walk(p);
      }
    };
    walk(dir);

    const parse = { g4: parseG4, y: parseYacc, jison: parseYacc, ebnf: parseEBNF, l: parseLex, jisonlex: parseLex };
    const failed = [];

    for(const f of files)
      try {
        parse[f.slice(f.lastIndexOf('.') + 1)](readFileSync(f, 'utf-8'), f);
      } catch(e) {
        failed.push(f + ': ' + e.message.slice(0, 80));
      }

    eq('', failed.join('\n'));
  },
});
