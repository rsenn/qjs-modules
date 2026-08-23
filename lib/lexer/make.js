import { define } from 'util';
import { Lexer } from 'lexer';
import { Token } from 'lexer';

/* Token rules for a portable Make dialect - the common core shared by
 * Borland make, Microsoft NMAKE and BSD bmake. GNU-only syntax (extra
 * assignment operators, conditionals, define/endef, include/vpath, ...)
 * lives in GNUMakeRules below, layered on top by GNUMakeLexer.
 *
 * A recipe line (one starting with a tab) is lexed as raw shell text in
 * a separate RECIPE state; entering it is decided by peeking at the
 * character right after a newline, so a tab used as ordinary mid-line
 * whitespace elsewhere is unaffected. $(...) / ${...} variable/function
 * references get their own VARREF state, entered and left via the
 * lexer's state stack, so nested calls (e.g. $(subst a,b,$(X))) balance
 * correctly without a hand-rolled depth counter. */
export const MakeRules = [
  ['comment', /<INITIAL>#[^\n]*/],
  ['bangDirective', /<INITIAL>![A-Za-z]+/],

  ['lineContinuation', /<INITIAL,RECIPE,VARREF,DEFINE>\\\r?\n/],
  ['newline', /<INITIAL,RECIPE>\r?\n/, lexer => (lexer.state = lexer.input[lexer.charPos] === '\t' ? 'RECIPE' : 'INITIAL')],

  ['dcolon', /<INITIAL>::/],
  ['colon', /<INITIAL>:/],
  ['semi', /<INITIAL>;/],
  ['assign', /<INITIAL>=/],
  ['pipe', /<INITIAL>\|/],
  ['percent', /<INITIAL>%/],
  ['comma', /<INITIAL,VARREF>,/],
  ['lparen', /<INITIAL>\(/],
  ['rparen', /<INITIAL>\)/],

  ['string', /<INITIAL>"(\\.|[^\\"\n])*"/],
  ['stringSingle', /<INITIAL>'(\\.|[^\\'\n])*'/],

  ['varOpen', /<INITIAL,RECIPE,VARREF,DEFINE>\$[({]/, lexer => lexer.pushState('VARREF')],
  ['varClose', /<VARREF>[)}]/, lexer => lexer.popState()],
  ['automaticVar', /<INITIAL,RECIPE,VARREF,DEFINE>\$[^({\n]/],
  ['varText', /<VARREF>[^${}()\n]+/],

  ['recipePrefix', /<RECIPE>\t/],
  ['recipeBackslash', /<RECIPE>\\/],
  ['recipeText', /<RECIPE>[^\n$\\]+/],

  ['backslash', /<INITIAL>\\/],
  ['bang', /<INITIAL>!/],
  ['name', /<INITIAL>[^\s:;=#\\$(){}%,"'|!]+/],
];

export class MakeLexer extends Lexer {
  constructor(input, mode = Lexer.LONGEST, filename, mask) {
    super(input, mode, filename, mask);

    this.addRules();
  }

  addRules() {
    for(const [name, ...rule] of MakeRules) this.addRule(name, ...rule);

    this.addRule('whitespace', /<INITIAL>[ \t]+/);
  }
}

globalThis.MakeLexer = MakeLexer;

define(MakeLexer.prototype, { [Symbol.toStringTag]: 'MakeLexer' });

/* GNU make dialect: conditionals, define/endef text blocks, extra
 * assignment operators, include/vpath/export directives. These rules
 * are registered before MakeRules (see GNUMakeLexer.addRules) so that,
 * on a length tie against the generic {name} rule, the keyword wins -
 * same "first rule wins" tie-break as lib/lexer/shell.js. */
export const GNUMakeRules = [
  ['ifeq', /<INITIAL>ifeq/],
  ['ifneq', /<INITIAL>ifneq/],
  ['ifdef', /<INITIAL>ifdef/],
  ['ifndef', /<INITIAL>ifndef/],
  ['else', /<INITIAL>else/],
  ['endif', /<INITIAL>endif/],
  ['includeIgnore', /<INITIAL>-include/],
  ['sinclude', /<INITIAL>sinclude/],
  ['include', /<INITIAL>include/],
  ['override', /<INITIAL>override/],
  ['export', /<INITIAL>export/],
  ['unexport', /<INITIAL>unexport/],
  ['undefine', /<INITIAL>undefine/],
  ['vpath', /<INITIAL>vpath/],
  ['private', /<INITIAL>private/],

  ['define', /<INITIAL>define/, lexer => lexer.pushState('DEFINE')],
  ['endef', /<DEFINE>endef\b[^\n]*/, lexer => lexer.popState()],
  ['defineNewline', /<DEFINE>\r?\n/],
  ['defineBackslash', /<DEFINE>\\/],
  ['defineBody', /<DEFINE>[^\n$\\]+/],

  ['assignImmediate', /<INITIAL>::=/],
  ['assignSimple', /<INITIAL>:=/],
  ['assignAppend', /<INITIAL>\+=/],
  ['assignConditional', /<INITIAL>\?=/],
  ['assignShell', /<INITIAL>!=/],
];

export class GNUMakeLexer extends MakeLexer {
  addRules() {
    for(const [name, ...rule] of GNUMakeRules) this.addRule(name, ...rule);

    super.addRules();
  }
}

globalThis.GNUMakeLexer = GNUMakeLexer;

define(GNUMakeLexer.prototype, { [Symbol.toStringTag]: 'GNUMakeLexer' });

export default GNUMakeLexer;
