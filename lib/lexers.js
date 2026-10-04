/* lexers.js: picks the lexer in lib/lexer/ that fits a file or a language.
 * depends on: lexer/*.js (one per language), path (basename, extname).
 * rule: a lexer is built per input and dropped after use, never shared. */
import { basename, extname } from 'path';
import BNFLexer from 'lexer/bnf.js';
import CLexer from 'lexer/c.js';
import CMakeLexer from 'lexer/cmake.js';
import CSVLexer from 'lexer/csv.js';
import ECMAScriptLexer from 'lexer/ecmascript.js';
import IniLexer from 'lexer/ini.js';
import { GNUMakeLexer } from 'lexer/make.js';
import ShellLexer from 'lexer/shell.js';
import XMLLexer from 'lexer/xml.js';

/* language name -> factory(source, fileName), each returning a lexer */
export const Lexers = {
  js: (str, file) => new ECMAScriptLexer(str, file),
  c: (str, file) => new CLexer(str, CLexer.LONGEST, file),
  bnf: (str, file) => new BNFLexer(str, file),
  csv: (str, file) => new CSVLexer(str, file),
  xml: (str, file) => new XMLLexer(str, file),
  sh: (str, file) => new ShellLexer(str, ShellLexer.LONGEST, file),
  cmake: (str, file) => new CMakeLexer(str, CMakeLexer.LONGEST, file),
  make: (str, file) => new GNUMakeLexer(str, GNUMakeLexer.LONGEST, file),
  ini: (str, file) => new IniLexer(str, IniLexer.LONGEST, file),
};

/* file extension -> language name, where the extension is not the name */
const Aliases = {
  h: 'c', hpp: 'c', hh: 'c', hxx: 'c', cc: 'c', cpp: 'c', cxx: 'c',
  mjs: 'js', cjs: 'js', json: 'js', ts: 'js',
  g4: 'bnf', ebnf: 'bnf', l: 'bnf', y: 'bnf', yy: 'bnf',
  html: 'xml', htm: 'xml', svg: 'xml',
  bash: 'sh',
  mk: 'make', mak: 'make',
};

const MakeBasenames = /^(GNUmakefile|makefile|Makefile)$/;
const has = (obj, key) => Object.prototype.hasOwnProperty.call(obj, key);

/* the language of a file, by base name first, then by extension.
 *
 *   string  file  path or file name: "src/a.cpp", "Makefile"
 *
 *   returns string|undefined  a key of Lexers ("c" for "src/a.cpp"), or
 *                             undefined when the file type is not known
 */
export function languageFor(file) {
  const base = basename(file);

  if(/\.(ini|mc[wp])$/i.test(base)) return 'ini';
  if(MakeBasenames.test(base)) return 'make';
  if(base == 'CMakeLists.txt') return 'cmake';

  const ext = extname(file).substring(1).toLowerCase();

  if(has(Lexers, ext)) return ext;
  if(has(Aliases, ext)) return Aliases[ext];
}

/* the lexer factory for a file, or for a language given by name.
 *
 *   string  file      path or file name, as for languageFor()
 *   string  language  a key of Lexers; default: languageFor(file)
 *
 *   returns function|undefined  (source, fileName) => lexer, or undefined
 *                               when there is no lexer for that file/language
 */
export function lexerFor(file, language = languageFor(file)) {
  return has(Lexers, language) ? Lexers[language] : undefined;
}
