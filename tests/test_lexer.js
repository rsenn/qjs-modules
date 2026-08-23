import { readFileSync } from 'fs';
import { basename, extname, join, dirname } from 'path';
import { getOpt } from 'util';
import BNFLexer from '../lib/lexer/bnf.js';
import CLexer from '../lib/lexer/c.js';
import CMakeLexer from '../lib/lexer/cmake.js';
import CSVLexer from '../lib/lexer/csv.js';
import ECMAScriptLexer from '../lib/lexer/ecmascript.js';
import IniLexer from '../lib/lexer/ini.js';
import { GNUMakeLexer } from '../lib/lexer/make.js';
import ShellLexer from '../lib/lexer/shell.js';
import XMLLexer from '../lib/lexer/xml.js';
import { puts } from 'std';

/* Universal syntax highlighter: dispatches to whichever lexer in
 * lib/lexer/*.js matches the file's extension, then reprints the file
 * with every token wrapped in an ANSI color picked by a small set of
 * generic categories (see classify() below) that hold across all of
 * these lexers regardless of their individual rule names. */

const Colors = {
  keyword: '\x1b[1;31m', // light red
  identifier: '\x1b[1;33m', // light yellow
  comment: '\x1b[1;32m', // light green
  punct: '\x1b[1;36m', // light cyan
  regex: '\x1b[1;35m', // light magenta
  other: '\x1b[0;37m', // light gray
};
const Reset = '\x1b[0m';

function classify({ type, lexeme }) {
  const t = (type ?? '').toLowerCase();

  if(/comment|preprocessor|directive/.test(t)) return 'comment';
  if(/regex|template/.test(t) || lexeme[0] == '`') return 'regex';
  if(t == 'keyword' || t == lexeme.toLowerCase()) return 'keyword';
  if((/string|literal|quoted/.test(t) && !/numeric|boolean|null/.test(t)) || lexeme[0] == '"' || lexeme[0] == "'") return 'punct';
  if(/identifier|name/.test(t)) return 'identifier';
  if(t == 'punctuator' || /^[^\w\s]+$/.test(lexeme)) return 'punct';
  return 'other';
}

const Lexers = {
  js: (str, file) => new ECMAScriptLexer(str, file),
  c: (str, file) => new CLexer(str, CLexer.LONGEST, file),
  bnf: (str, file) => new BNFLexer(str, file),
  csv: (str, file) => new CSVLexer(str, file),
  xml: (str, file) => new XMLLexer(str, file),
  sh: (str, file) => new ShellLexer(str, ShellLexer.LONGEST, file),
  cmake: (str, file) => new CMakeLexer(str, CMakeLexer.LONGEST, file),
  make: (str, file) => new GNUMakeLexer(str, GNUMakeLexer.LONGEST, file),
};

Lexers.h = Lexers.hpp = Lexers.cc = Lexers.cpp = Lexers.c;
Lexers.mjs = Lexers.cjs = Lexers.json = Lexers.ts = Lexers.js;
Lexers.g4 = Lexers.ebnf = Lexers.l = Lexers.y = Lexers.bnf;
Lexers.html = Lexers.htm = Lexers.svg = Lexers.xml;
Lexers.bash = Lexers.sh;
Lexers.mk = Lexers.mak = Lexers.make;

const MakeBasenames = /^(GNUmakefile|makefile|Makefile)$/;

function lexerFor(file) {
  const base = basename(file);

  if(MakeBasenames.test(base)) return Lexers.make;
  if(base == 'CMakeLists.txt') return Lexers.cmake;

  return Lexers[extname(file).substring(1).toLowerCase()];
}

function highlight(file) {
  const str = readFileSync(file, 'utf-8');
  const make = lexerFor(file);

  if(!make) {
    puts(`# skipping '${file}': no lexer for this file type\n`);
    return;
  }

  const lexer = make(str, file);

  for(const tok of lexer) puts(Colors[classify(tok)] + tok.lexeme + Reset);

  puts('\n');
}

function main(...args) {
  const params = getOpt({ '@': 'file' }, args);
  let files = params['@'];

  if(!files.length) files = [join(dirname(process.argv[1]), '..', 'lib', 'util.js')];

  for(const file of files) highlight(file);
}

main(...scriptArgs.slice(1));
