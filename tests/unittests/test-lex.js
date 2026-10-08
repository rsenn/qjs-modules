/* lib/lexer/lex.js and lib/parser/lex.js: flex source, its AST, and the
 * lexers built from it. Each scan builds one Lexer and reads it to the end. */
import LexLexer from '../../lib/lexer/lex.js';
import { parseLex, parseRegex, Rule, Definition, Eof } from '../../lib/parser/lex.js';
import { eq, tests } from '../../lib/tinytest.js';

const SAMPLE = `%{
#include "y.tab.h"
%}
%option noyywrap
%x COMMENT
%s INCL
D\t[0-9]
L\t[a-zA-Z_]

%%
"/*"\t\t{ BEGIN(COMMENT); }
<COMMENT>"*/"\t{ BEGIN(INITIAL); }
<COMMENT>.|\\n\t;
{L}({L}|{D})*\t{ return IDENT; }
{D}+/\\.\\.\t{ return INT; }
"if"|"fi"\t|
"then"\t{ return KEYWORD; } /* tail */
[ \\t\\n]+\t/* skip */
<<EOF>>\t{ return 0; }
%%
int main() { return 0; }
`;

const scan = src => [...new LexLexer(src, LexLexer.FIRST, 'a.l')].map(t => `${t.type}:${t.lexeme}`);

const tokens = (file, input) => [...file.scan(input)].map(t => `${t.type}:${t.lexeme}`);

tests({
  'lexer: the three sections'() {
    eq(
      ['def_name:D', 'def_regex:[0-9]', 'section:%%', 'regex:{D}+', 'action:{ return N; }', 'user_section:%%', 'user:\nmain() {}'].join(),
      scan('D [0-9]\n%%\n{D}+ { return N; }\n%%\nmain() {}').join(),
    );
  },

  'lexer: %{ %} blocks, directives and comments'() {
    eq('codeblock:%{\nint x;\n%},directive:%option,d_word:noyywrap,comment:/* c */', scan('%{\nint x;\n%}\n%option noyywrap\n/* c */\n').join());
  },

  'lexer: a nested action is one token'() {
    eq('section:%%,regex:a,action:{ if(x) { y("}"); } }', scan('%%\na { if(x) { y("}"); } }\n').join());
  },

  'lexer: a start condition and <<EOF>>'() {
    eq('section:%%,condition:<A,B>,regex:x,action_line:;,eof:<<EOF>>,action:{ }', scan('%%\n<A,B>x ;\n<<EOF>> { }\n').join());
  },

  'parser: toString() gives the source back'() {
    eq(SAMPLE, String(parseLex(SAMPLE, 'a.l')));
  },

  'parser: definitions, directives and rules'() {
    const file = parseLex(SAMPLE, 'a.l');
    const defs = file.definitions.filter(d => d instanceof Definition);
    const rules = file.rules.filter(r => r instanceof Rule);

    eq('D,L', defs.map(d => d.name).join());
    eq('COMMENT,true,INCL,false', file.states.flat().join());
    eq(9, rules.length);
    eq('COMMENT', rules[1].conditions.join());
    eq(true, rules[rules.length - 1].pattern instanceof Eof);
    eq('int main() { return 0; }\n', file.user.slice(1));
  },

  'parser: a pattern becomes a lexer pattern'() {
    eq('(?:ab)', parseRegex('"ab"').toJS());
    eq('{D}+', parseRegex('{D}+').toJS());
    eq('[a-zA-Z_]', parseRegex('[[:alpha:]_]').toJS().replace('A-Za-z', 'a-zA-Z'));
    eq('[^\\n]', parseRegex('.').toJS());
    eq('(?<![^\\n])a(?![^\\n])', parseRegex('^a$').toJS());
    eq('a(?=b)', parseRegex('a/b').toJS());
    eq('(?:a|b)*', parseRegex('(a|b)*').toJS());
    eq('\\x0a', parseRegex('\\n').toJS() == '\\n' ? '\\x0a' : '');
  },

  'parser: a regex is flex source again'() {
    for(const src of ['a|b', '"x"+', '[^"\\n]*', '({L}|{D}){2,3}', 'a/b', '^x$', '(?i:ab)']) eq(src, String(parseRegex(src)));
  },

  'scan: longest match, first rule on a tie'() {
    const file = parseLex('L [a-z]\n%%\nif\t{ return IF; }\n{L}+\t{ return ID; }\n[ ]+\t;\n');

    eq('IF:if,ID:iff,ID:x', tokens(file, 'if iff x').join());
  },

  'scan: a rule without return skips, BEGIN() switches state'() {
    const file = parseLex('%x STR\n%%\n\\"\t{ BEGIN(STR); }\n<STR>\\"\t{ BEGIN(INITIAL); }\n<STR>[^\\"]+\t{ return TEXT; }\n[a-z]+\t{ return WORD; }\n[ ]+\t;\n');

    eq('WORD:a,TEXT:b c,WORD:d', tokens(file, 'a "b c" d').join());
  },

  'scan: a | action shares the next rule'() {
    const file = parseLex('%%\n"a"\t|\n"b"\t{ return AB; }\n');

    eq('AB:a,AB_2:b', tokens(file, 'ab').join());
  },

  'scan: a return of a name from a call or a char is a plain token'() {
    const file = parseLex('%%\n"x"\t{ return(X); }\n"y"\t{ return Y; }\n');

    eq('X:x,Y:y', tokens(file, 'xy').join());
  },

  'scan: a <STATE>{ } block gives its rules that start condition'() {
    const src = '%x C\n%%\n"/*"\t{ BEGIN(C); }\n<C>{\n"*/"\t{ BEGIN(INITIAL); }\n.|\\n\t;\n}\n[a-z]+\t{ return W; }\n[ ]+\t;\n';
    const file = parseLex(src);

    eq(src, String(file));
    eq('W:a,W:b', tokens(file, 'a /* x */ b').join());
  },

  'parser: a jison pattern may be quoted with single quotes'() {
    const src = "%%\n'\"'[^\"]+'\"'\treturn 'STRING';\n";

    eq(src, String(parseLex(src)));
  },

  'toLexerSource: a Lexer subclass with the same rules'() {
    const src = parseLex('D [0-9]\n%%\n{D}+\t{ return NUM; }\n[ ]+\t;\n').toLexerSource('NumLexer');

    eq(true, src.includes('export class NumLexer extends Lexer'));
    eq(true, src.includes('this.define("D", /(?:[0-9])/);'));
    eq(true, src.includes('this.addRule("NUM", /<INITIAL>{D}+/);'));
    eq(true, src.includes('(lexer, skip) => { skip(); }'));
  },
});
