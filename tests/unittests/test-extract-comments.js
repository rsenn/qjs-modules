/* utilities/extract-comments.js: what counts as a comment, grouping, doc
 * detection, the filters, and the text/JSON shapes.
 * Each scan builds one Lexer and reads it to the end before the next. */
import { extractComments, toLines, toObject } from '../../utilities/extract-comments.js';
import { assert, assertEquals, tests } from '../../lib/tinytest.js';

/* eq() of tinytest compares arrays and objects by reference */
const eq = (expected, actual) => assertEquals(JSON.stringify(expected), JSON.stringify(actual));

const scan = (src, language, opts) => [...extractComments(src, language, opts)];
const bodies = (src, language, opts) => scan(src, language, opts).map(r => r.body);
const kinds = (src, language) => scan(src, language).map(r => r.kind + (r.doc ? ':' + r.doc : ''));

function lexError(src, language) {
  const got = [];

  try {
    for(const rec of extractComments(src, language)) got.push(rec.body);
  } catch(e) {
    return { got, message: e.message };
  }

  return { got };
}

tests({
  'c: line and block comments, and none inside strings'() {
    eq(['// a', '/* b */'], bodies('int a; // a\nchar *s = "// no"; /* b */ char c = \'"\';\n', 'c'));
  },

  'c: a comment after #define or #include is found, <a//b.h> is not one'() {
    eq(['/* doc */', '// inc'], bodies('#define X 1 /* doc */\n#include <a//b.h> // inc\n', 'c'));
  },

  'c: a block comment left open by a preprocessor line ends where it ends'() {
    eq(['/* open\n  still */', '// after'], bodies('#define Z 2 /* open\n  still */ int q; // after\n', 'c'));
  },

  'js: a comment after a division, a regexp literal after =, a string'() {
    eq(['// x', '// w'], bodies('let a = b / c; // x\nlet r = /y\\/z/g; // w\nlet s = "// no";\n', 'js'));
  },

  'js: a regexp literal after the ) of an if, division after the ) of a call'() {
    eq(['// y', '// z'], bodies('if (a) /x/.test(b); // y\nf(a) / 2; // z\n', 'js'));
  },

  'js: the shebang line is not a comment'() {
    eq(['// first'], bodies('#!/usr/bin/env qjsm\n// first\n', 'js'));
  },

  'sh: the shebang is not a comment; $#, ${#x} and quoted # are not'() {
    eq(['# real', '# tail'], bodies('#!/bin/sh\n# real\necho $# ${#x} "a # b" # tail\n', 'sh'));
  },

  'ini: a # inside a value is not a comment'() {
    eq(['; c'], bodies('[s]\nurl=http://a#b\ncolor=#fff\n; c\n', 'ini'));
  },

  'make: \\# is not a comment, a recipe comment is'() {
    eq(['# top', '# c', '# recipe'], bodies('# top\nB=\\#x # c\nall:\n\techo hi # recipe\n', 'make'));
  },

  'cmake: line and bracket comments, none in a quoted argument'() {
    eq(['# c', '#[[ b\nc ]]'], bodies('set(A "# no") # c\n#[[ b\nc ]]\n', 'cmake'));
  },

  'xml: a comment, but not one inside an attribute value'() {
    eq(['<!-- yes -->'], bodies('<a b="<!-- no -->"><!-- yes --></a>\n', 'xml'));
  },

  'bnf: (* *), //, # and /* */ comments; quoted literals and %{ %} are skipped'() {
    eq(['(* ebnf *)', '// line', '# hash', '/* c */'], bodies('(* ebnf *)\na = "(*" ; // line\nb = \'/*\' ; # hash\n%{\n#include <x>\n%}\n/* c */\n', 'bnf'));
  },

  'bnf: comments of BNF, ISO EBNF and W3C EBNF'() {
    eq(['# note'], bodies('<a> ::= "x" # note\n', 'bnf', { file: 'a.bnf' }));
    eq(['(* a *)', '(* b *)'], bodies('(* a *)\nx = "(*" , { y } ; (* b *)\n', 'bnf', { file: 'a.ebnf' }));
    eq(['/* c */'], bodies('/* c */\nA ::= #x9 | #xA | [#x20-#xD7FF]\n', 'bnf', { file: 'w.ebnf' }));
  },

  'bnf: yacc comments in code, but #include is not one'() {
    const src = '%{\n#include <x> // inc\n%}\n%%\na : b { /* act */ } ; // end\n%%\n#include <y.h>\nint f(){ return 0; } /* tail */\n';

    eq(['// inc', '/* act */', '// end', '/* tail */'], bodies(src, 'bnf', { file: 'g.y' }));
  },

  'csv has no comments'() {
    eq([], bodies('a,b\n// not one\n', 'csv'));
  },

  'adjacent line comments are one comment, a blank line ends it'() {
    eq(['// a\n// b', '// c'], bodies('// a\n// b\n\n// c\n', 'c'));
  },

  '--group-gap bridges blank lines, --no-group joins nothing'() {
    eq(['// a\n// b\n\n// c'], bodies('// a\n// b\n\n// c\n', 'c', { gap: 1 }));
    eq(['// a', '// b', '// c'], bodies('// a\n// b\n// c\n', 'c', { group: false }));
  },

  'a comment after code is never grouped, nor are different markers'() {
    eq(['// d', '// e'], bodies('int x; // d\n// e\n', 'c'));
    eq(['/// a', '// b'], bodies('/// a\n// b\n', 'c'));
  },

  'a group reports where it starts and ends'() {
    const [rec] = scan('int a;\n  // a\n  // b\n', 'c');

    eq([2, 3, 3, 2], [rec.line, rec.column, rec.endLine, rec.lines]);
  },

  'c: which comments are doc comments'() {
    eq(['block:doxygen'], kinds('/** a */', 'c'));
    eq(['block:doxygen'], kinds('/*! a */', 'c'));
    eq(['block:doxygen'], kinds('/**< a */', 'c'));
    eq(['line:doxygen'], kinds('/// a', 'c'));
    eq(['line:doxygen'], kinds('//! a', 'c'));
    eq(['line:doxygen'], kinds('int x; ///< a', 'c'));
    eq(['block', 'block', 'line'], kinds('/**/ /*** a */ //// a', 'c').slice(0, 3));
  },

  'js: only /** */ is a doc comment, as jsdoc'() {
    eq(['block:jsdoc', 'block', 'line'], kinds('/** a */ /* b */ // c', 'js'));
    eq(['line'], kinds('/// <reference path="a" />', 'js'));
  },

  'filter by kind'() {
    const src = '// a\n/* b */\n// c\n// d\n';

    eq(['// a', '// c\n// d'], bodies(src, 'c', { types: ['line'] }));
    eq(['/* b */'], bodies(src, 'c', { types: ['block'] }));
  },

  'filter doc comments: only, none, one style'() {
    const src = '/** a */\n/* b */\n/// c\n';

    eq(['/** a */', '/// c'], bodies(src, 'c', { doc: true }));
    eq(['/* b */'], bodies(src, 'c', { doc: false }));
    eq(['/** a */'], bodies('/** a */ // b\n', 'js', { style: 'jsdoc' }));
    eq([], bodies('/** a */\n', 'js', { style: 'doxygen' }));
  },

  'filter by the number of lines'() {
    const src = '/* 1 */\n/* 1\n2\n3 */\n// a\n// b\n// c\n// d\n// e\n';

    eq(['/* 1\n2\n3 */', '// a\n// b\n// c\n// d\n// e'], bodies(src, 'c', { minLines: 3 }));
    eq(['/* 1 */'], bodies(src, 'c', { maxLines: 1 }));
    eq(['/* 1\n2\n3 */'], bodies(src, 'c', { minLines: 2, maxLines: 4 }));
  },

  'C++ comments on several lines (type line, 2+ lines)'() {
    eq(['// a\n// b'], bodies('// a\n// b\nint x; // c\n/* d\ne */\n', 'c', { types: ['line'], minLines: 2 }));
  },

  'filter by a pattern, inverted, on the text as printed'() {
    const src = '// TODO: x\n\n// note\n';

    eq(['// TODO: x'], bodies(src, 'c', { pattern: /todo/i }));
    eq(['// note'], bodies(src, 'c', { pattern: /todo/i, invert: true }));
    eq([], bodies(src, 'c', { pattern: /^TODO/m }));
    eq(['TODO: x'], bodies(src, 'c', { pattern: /^TODO/m, strip: true }));
  },

  'strip: line markers, block stars, doc markers'() {
    eq(['a\nb'], bodies('// a\n// b\n', 'c', { strip: true }));
    eq(['a'], bodies('/// a\n', 'c', { strip: true }));
    eq(['a'], bodies('/** a */', 'c', { strip: true }));
    eq(['a\nb'], bodies('/*\n * a\n * b\n */', 'c', { strip: true }));
    eq(['a'], bodies('# a', 'sh', { strip: true }));
    eq(['a'], bodies('<!-- a -->', 'xml', { strip: true }));
    eq(['a'], bodies('(* a *)', 'bnf', { strip: true }));
    eq(['a\nb'], bodies('#[=[ a\nb ]=]', 'cmake', { strip: true }));
    eq([''], bodies('/**/', 'c', { strip: true }));
  },

  'text: one line per comment line, with the file name for several files'() {
    const [rec] = scan('int a;\n// a\n// b\n', 'c', { file: 'x.c' });

    eq(['2: // a\n', '3: // b\n'], toLines(rec, false));
    eq(['x.c:2: // a\n', 'x.c:3: // b\n'], toLines(rec, true));
  },

  'text: stripped lines keep the line numbers of the source'() {
    const [rec] = scan('/**\n * hello\n * world\n */\n', 'c', { strip: true, file: 'a.c' });

    eq(['2: hello\n', '3: world\n'], toLines(rec, false));
  },

  'json: the comment property and the position modes'() {
    const [rec] = scan('int a;\n  /* b */\n', 'c', { file: 'a.c' });

    eq({ comment: '/* b */', kind: 'block', loc: { line: 2, column: 3, file: 'a.c' } }, toObject(rec));
    eq({ k: '/* b */', kind: 'block', range: { start: 9, end: 16, file: 'a.c' } }, toObject(rec, { modes: ['range'], key: 'k' }));
    eq({ comment: '/* b */', kind: 'block', line: 2, column: 3, offset: 9, file: 'a.c', start: 'a.c:2:3', end: 'a.c:2:10' }, toObject(rec, { modes: ['line', 'offset', 'file', 'start', 'end'] }));
  },

  'json: doc comments carry their style'() {
    eq('jsdoc', toObject(scan('/** a */', 'js')[0]).doc);
  },

  'offsets are UTF-16 string indices, also after an emoji'() {
    const src = 'x = "😀"; // c\n',
      [rec] = scan(src, 'js');

    eq([10, 14, 11], [rec.offset, rec.end, rec.column]);
    eq('// c', src.slice(rec.offset, rec.end));
  },

  'non-ASCII text is kept as it is'() {
    eq(['/* a – b */', '// ünï 😀 end', '/* z */'], bodies('/* a – b */\nlet x = "é"; // ünï 😀 end\nlet y; /* z */\n', 'js'));
    eq(['<!-- ü -->', '<!-- z -->'], bodies('<a>é<!-- ü --><b>😀</b><!-- z --></a>\n', 'xml'));
  },

  'CRLF line endings stay out of the text'() {
    eq(['// a\n// b'], bodies('// a\r\n// b\r\n', 'c'));
  },

  'a lexer error ends the scan, after the comments found before it'() {
    const { got, message } = lexError('let a; // c1\n#!x\n// c2\n', 'js');

    eq(['// c1'], got);
    assert(/No matching token/.test(message), message);
  },

  'many files in a row: every language, several times'() {
    const samples = [
      ['js', 'let a = b / c; // x\n/** d */\n'],
      ['c', '#define X 1 /* a */\n// b\n'],
      ['sh', '#!/bin/sh\n# a\n'],
      ['ini', '; a\nk=v\n'],
      ['xml', '<a><!-- a --></a>\n'],
      ['make', '# a\nB=\\#x\n'],
      ['cmake', '# a\nset(A b)\n'],
      ['bnf', '(* a *)\nb = c ;\n'],
    ];

    for(let round = 0; round < 4; round++) for(const [language, src] of samples) assert(scan(src, language).length >= 1, `${language} round ${round}`);
  },
});
