/* lib/lexer/bnf.js: BNF, ISO and W3C EBNF, and the yacc/ANTLR dialect.
 * Each scan builds one Lexer and reads it to the end before the next. */
import BNFLexer from '../../lib/lexer/bnf.js';
import { assert, assertEquals, tests } from '../../lib/tinytest.js';

/* eq() of tinytest compares arrays and objects by reference */
const eq = (expected, actual) => assertEquals(JSON.stringify(expected), JSON.stringify(actual));

const SKIP = new Set(['ws', 'x_ws']);

/* "type:lexeme" of every token but whitespace; the file name picks the dialect */
function scan(src, file = 'a.ebnf') {
  const lexer = new BNFLexer(src, file),
    out = [];

  for(let tok; (tok = lexer.nextToken()); ) if(!SKIP.has(tok.type)) out.push(`${tok.type}:${tok.lexeme}`);

  return out;
}

const types = (src, file) => scan(src, file).map(t => t.slice(0, t.indexOf(':')));

tests({
  'bnf: <names>, ::= and double-quoted literals'() {
    eq(['nonterminal:<expr>', 'assign:::=', 'nonterminal:<term>', 'bar:|', 'string:"+"', 'semi:;'], scan('<expr> ::= <term> | "+" ;', 'a.bnf'));
  },

  'bnf: a name may contain spaces'() {
    eq(['nonterminal:<digit string>', 'assign:::='], scan('<digit string> ::='));
  },

  'ebnf: { } repeats, [ ] is an option, ? ? is a special sequence'() {
    eq(
      ['identifier:a', 'equals:=', 'identifier:b', 'comma:,', 'lbrace:{', 'identifier:c', 'rbrace:}', 'comma:,', 'lbracket:[', 'identifier:d', 'rbracket:]', 'comma:,', 'special:? sp ?', 'semi:;'],
      scan('a = b , { c } , [ d ] , ? sp ? ;'),
    );
  },

  'ebnf: a repetition count and an exception'() {
    eq(['number:3', 'asterisk:*', 'identifier:a', 'minus:-', 'string:"b"'], scan('3 * a - "b"'));
  },

  'ebnf: a ? after a name or ) is the optional mark, not a special sequence'() {
    eq(['identifier:a', 'question:?', 'identifier:b', 'question:?'], scan('a? b?'));
    eq(['lparen:(', 'identifier:a', 'rparen:)', 'question:?', 'identifier:b', 'question:?'], scan('(a)? b?'));
  },

  'ebnf: W3C #x escapes, character classes and exceptions'() {
    eq(['hex:#x9', 'bar:|', 'char_class:[#x20-#xD7FF]', 'bar:|', 'char_class:[a-z]', 'minus:-', 'literal:\'b\''], scan("#x9 | [#x20-#xD7FF] | [a-z] - 'b'"));
  },

  'ebnf: [ a ] with spaces or quotes is an option, not a class'() {
    eq(['lbracket:[', 'identifier:a', 'rbracket:]'], scan('[ a ]'));
    eq(['lbracket:[', 'literal:\'a\'', 'rbracket:]'], scan("['a']"));
  },

  'comments are tokens: /* */, //, # and (* *)'() {
    eq(['multiline_comment:/* a */', 'singleline_comment:// b', 'hash_comment:# c', 'ebnf_comment:(* d *)'], scan('/* a */\n// b\n# c\n(* d *)\n'));
  },

  'a comment with * and ) inside ends at the first *)'() {
    eq(['ebnf_comment:(* a * b ) c *)', 'identifier:x'], scan('(* a * b ) c *) x'));
  },

  'ebnf: #x9 is a hex escape, # x9 is a comment'() {
    eq(['hex:#x9'], scan('#x9'));
    eq(['hash_comment:# x9 and more'], scan('# x9 and more'));
  },

  'a quoted literal hides comment markers'() {
    eq(['string:"(*"', 'literal:\'/*\'', 'string:"//"', 'string:"#"'], scan('"(*" \'/*\' "//" "#"'));
  },

  'ebnf: any other character is a token, not an error'() {
    eq(['identifier:a', 'equals:=', 'identifier:b', 'other:&', 'identifier:c', 'other:^'], scan('a = b & c ^'));
  },

  'a name that starts with "grammar" is one identifier'() {
    eq(['identifier:grammar_rule', 'equals:='], scan('grammar_rule ='));
    eq(['keyword:grammar', 'identifier:Foo', 'semi:;'], scan('grammar Foo;', 'a.g4'));
  },

  'yacc: { } is embedded code, with its comments'() {
    const t = types('a : b { /* c */ x = 1; } ;', 'a.y');

    assert(t.includes('multiline_comment'), t.join());
    eq('semi', t[t.length - 1]);
  },

  'ebnf: { } is not embedded code'() {
    eq(['lbrace:{', 'identifier:c', 'rbrace:}'], scan('{ c }', 'a.ebnf'));
    eq(['lbrace:{', 'identifier:c', 'rbrace:}'], scan('{ c }', '<stdin>'));
  },

  'yacc: #include in %{ %} and after the second %% is C, not a comment'() {
    const t = types('%{\n#include <a.h>\n%}\n%%\na : b ;\n%%\n#include <b.h>\nint f() { return 1; }\n', 'g.y');

    assert(!t.includes('hash_comment'), t.join());
    eq(2, t.filter(x => x == 'c_preprocessor').length);
  },

  'yacc: tokens of the grammar itself are unchanged'() {
    eq(['directive:%token', 'd_identifier:A', 'd_newline:\n', 'section2:%%', 'identifier:s', 'colon::', 'identifier:A', 'semi:;'], scan('%token A\n%%\ns : A ;', 'g.y'));
  },

  'antlr: classes with spaces, literals, arrows and // comments'() {
    eq(
      ['identifier:WS', 'colon::', 'char_class:[ \\t\\r\\n]', 'plus:+', 'arrow:->', 'identifier:skip', 'semi:;', 'singleline_comment:// c'],
      scan('WS : [ \\t\\r\\n]+ -> skip ; // c', 'a.g4'),
    );
  },

  'many grammars in a row'() {
    for(let i = 0; i < 10; i++) {
      assert(scan('<a> ::= "x" (* c *)', 'a.bnf').length == 4, 'bnf ' + i);
      assert(scan('%token A\n%%\ns : A ;', 'g.y').length > 4, 'yacc ' + i);
    }
  },
});
