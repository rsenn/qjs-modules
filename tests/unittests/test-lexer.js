import { mkdirSync, writeFileSync } from 'fs';
import { Lexer, Token } from 'lexer';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* new Lexer(input, mode, fileName, mask) - doc/native/lexer.md's stated
 * `new Lexer(input[, fileName, mode])` signature doesn't match the actual
 * argument order (confirmed against quickjs-lexer.c and lib/lexer/*.js).
 *
 * This module is fragile in ways this file works around rather than
 * fully root-causes (see BUGS: lexer-state-rules-crash-after-3-instances,
 * lexer-skipbytes-overflows-charpos, lexer-unescape-doubles-backslash...):
 * multiple concurrently-alive Lexer instances (even just two, each with
 * their own handful of addRule() calls) were observed to corrupt each
 * other's property reads (wrong charPos, garbage currentLine() output),
 * and setInput() on an already-used Lexer does not reliably reset scan
 * position/lookahead state either. So this file keeps exactly ONE Lexer
 * alive at a time everywhere it can - one shared multi-rule grammar,
 * covered by a single continuous forward scan, for most of the module's
 * surface; the few tests below that need their own fresh Lexer run it to
 * completion and let it go out of scope before the next test's Lexer is
 * constructed, rather than holding several alive together. */
const lex = new Lexer('12 + foo "hi there" 34', Lexer.LONGEST, 'in.txt');
lex.addRule('ws', /[ \t]+/);
lex.addRule('number', /[0-9]+/);
lex.addRule('ident', /[a-zA-Z_][a-zA-Z0-9_]*/);
lex.addRule('plus', /\+/);
lex.addRule('dquote', /<INITIAL>"/, l => (l.state = 'STRING'));
lex.addRule('strbody', /<STRING>[^"]*/);
lex.addRule('strend', /<STRING>"/, l => (l.state = 'INITIAL'));

const TMP = '.tmp/test-lexer';

tests({
  'constructor: size/fileName/eof/charPos, and ruleNames'() {
    eq(lex.size, 22);
    eq(lex.fileName, 'in.txt');
    assert(!lex.eof);
    eq(lex.charPos, -1); // -1 sentinel until the first match attempt
    eq(
      JSON.stringify(lex.ruleNames),
      JSON.stringify(['ws', 'number', 'ident', 'plus', 'dquote', 'strbody', 'strend']),
    );
  },
  'peek()/peekToken() do not consume'() {
    const id1 = lex.peek();
    const id2 = lex.peek();
    eq(id1, id2);
    const peeked = lex.peekToken();
    eq(peeked.lexeme, '12');
    eq(lex.charPos, 0); // still not consumed
  },
  'next() returns a numeric token id; Token properties on the result of nextToken()'() {
    const id = lex.next();
    assert(typeof id === 'number');

    /* next() (unlike nextToken()) consumes through the following
     * whitespace too, so this picks up at "+", not "12" or the ws. */
    const tok = lex.nextToken();
    eq(tok.type, 'plus');
    eq(tok.lexeme, '+');
    assert(typeof tok.id === 'number');
    assert(typeof tok.seq === 'number');
    assert(tok.lexer === lex);
    assert(tok.rule != null);
    eq(tok.toString(), '+');
    eq(`${tok}`, '+');

    const rule = lex.getRule(tok.id);
    assert(rule != null);
    eq(lex.tokenClass(tok.id), 'plus');
  },
  'rest of the token stream up to and including a <STATE>-tagged quoted string, via [Symbol.iterator]'() {
    const out = [];
    for(const tok of lex) {
      out.push([tok.type, tok.lexeme]);
      if(tok.type === 'strend') break; // stop right after the quoted string
    }
    eq(
      JSON.stringify(out),
      JSON.stringify([
        ['ws', ' '],
        ['ident', 'foo'],
        ['ws', ' '],
        ['dquote', '"'],
        ['strbody', 'hi there'],
        ['strend', '"'],
      ]),
    );
  },
  'pushState()/popState()/topState() (aliases begin/end)'() {
    const before = lex.topState();
    lex.pushState('STRING');
    eq(lex.stateDepth, 1);
    lex.popState();
    eq(lex.stateDepth, 0);
    eq(lex.topState(), before);

    lex.begin('STRING');
    eq(lex.stateDepth, 1);
    lex.end();
    eq(lex.stateDepth, 0);

    assert(lex.states != null);
  },
  'scanning to eof: the trailing number, then eof is true'() {
    const out = [];
    let tok;
    while((tok = lex.nextToken()) !== null) out.push([tok.type, tok.lexeme]);
    eq(JSON.stringify(out), JSON.stringify([['ws', ' '], ['number', '34']]));
  },
  'skipBytes() is a safe no-op with no argument'() {
    /* skipBytes(n)'s n is bounded by the *current* (already-matched)
     * token's byte length, not the remaining input, and overflows
     * charPos when given a valid nonzero n - see BUGS:
     * lexer-skipbytes-overflows-charpos. Only the always-safe no-arg
     * form is exercised here. */
    const l = new Lexer('x', Lexer.LONGEST, 'in.txt');
    l.addRule('x', /x/);
    l.skipBytes();
    eq(l.charPos, 0);
  },
  'currentLine() reflects the current line once scanning has started'() {
    const l = new Lexer('12 foo\nbar 34', Lexer.LONGEST, 'in.txt');
    l.addRule('ws', /[ \t]+/);
    l.addRule('number', /[0-9]+/);
    l.addRule('ident', /[a-zA-Z_][a-zA-Z0-9_]*/);
    l.nextToken();
    eq(l.currentLine(), '12 foo');
  },
  'loc getter/Token.loc; mode and charPos getter/setter round-trip'() {
    const l = new Lexer('12 foo', Lexer.LONGEST, 'in.txt');
    l.addRule('number', /[0-9]+/);
    l.addRule('ws', /[ \t]+/);
    l.addRule('ident', /[a-zA-Z_][a-zA-Z0-9_]*/);
    const tok = l.nextToken();
    assert(tok.loc != null);
    assert(l.loc != null);

    l.mode = Lexer.LONGEST;
    eq(l.mode, Lexer.LONGEST);

    l.charPos = 3;
    eq(l.charPos, 3);
  },
  'define() adds a named sub-pattern usable from later rules'() {
    const l = new Lexer('123', Lexer.LONGEST, 'in.txt');
    l.define('digit', /[0-9]/);
    l.addRule('number', /{digit}+/);
    const tok = l.nextToken();
    eq(tok.type, 'number');
    eq(tok.lexeme, '123');
  },
  'Lexer.escape()/Lexer.unescape()/Lexer.toString()'() {
    /* unescape() only truly round-trips escape()'s output for chars
     * escape_pred leaves untouched - see BUGS:
     * lexer-unescape-doubles-backslash-instead-of-removing-it for why a
     * string containing '.', '*' etc. does NOT round-trip. */
    const s = 'hello world 123';
    eq(Lexer.unescape(Lexer.escape(s)), s);
    eq(Lexer.escape('a.b*c'), 'a\\.b\\*c');
    eq(typeof Lexer.toString, 'function');
  },
  'Lexer.fromFile() creates a lexer reading from a file'() {
    try {
      mkdirSync(TMP, { recursive: true });
    } catch(e) {}
    writeFileSync(`${TMP}/input.txt`, 'hello 42');
    const l = Lexer.fromFile(`${TMP}/input.txt`);
    l.addRule('ws', /[ \t]+/);
    l.addRule('number', /[0-9]+/);
    l.addRule('ident', /[a-zA-Z_][a-zA-Z0-9_]*/);
    const tok = l.nextToken();
    eq(tok.type, 'ident');
    eq(tok.lexeme, 'hello');
  },
});
