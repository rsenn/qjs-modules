/* Lexer on a Reader: `new Lexer({ read(buf, len) })` scans the same tokens as the
 * in-memory input, however the stream is cut into reads. One Lexer is alive at a time. */
import * as std from 'std';
import { mkdirSync, writeFileSync } from 'fs';
import { Lexer } from 'lexer';
import { assert, eq, tests } from '../../lib/tinytest.js';

const bytes = s => Uint8Array.from(unescape(encodeURIComponent(s)), c => c.charCodeAt(0));

/* an object with read(buf, len), delivering at most `chunk` bytes per call */
function source(text, chunk) {
  const data = bytes(text);
  let pos = 0;

  return {
    reads: 0,
    read(buf, len) {
      const n = Math.min(len, chunk, data.length - pos);

      new Uint8Array(buf).set(data.subarray(pos, pos + n));
      pos += n;
      this.reads++;
      return n;
    },
  };
}

function grammar(input, mode) {
  const lex = new Lexer(input, mode, 'in.txt');

  lex.addRule('ws', /[ \t\n]+/);
  lex.addRule('comment', /\/\*[^]*?\*\//);
  lex.addRule('number', /[0-9]+/);
  lex.addRule('ident', /[a-zA-Z_][a-zA-Z0-9_]*/);
  lex.addRule('plus', /\+/);
  lex.addRule('dquote', /<INITIAL>"/, l => (l.state = 'STRING'));
  lex.addRule('strbody', /<STRING>[^"]+/);
  lex.addRule('strend', /<STRING>"/, l => (l.state = 'INITIAL'));
  return lex;
}

const scan = lex => {
  const out = [];
  let tok;

  while((tok = lex.nextToken()) !== null) out.push(`${tok.type}:${tok.lexeme}@${tok.loc.line}:${tok.loc.column}`);
  return out.join(' ');
};

const TEXT = '12 + foo /* a \u03b1\u03b2 comment\nover several lines */ "h\u00e9llo \u4e16\u754c" + bar9\n"x" 007';
const long = 'a' + 'b'.repeat(5000);

tests({
  'reader: same tokens as the in-memory input at every chunk size'() {
    const want = scan(grammar(TEXT, Lexer.LONGEST));

    for(const chunk of [1, 2, 3, 5, 7, 64, 8192]) eq(scan(grammar(source(TEXT, chunk), Lexer.LONGEST)), want, `chunk ${chunk}`);
  },

  'reader: FIRST mode matches as well'() {
    const want = scan(grammar(TEXT, Lexer.FIRST));

    for(const chunk of [1, 4, 100]) eq(scan(grammar(source(TEXT, chunk), Lexer.FIRST)), want, `chunk ${chunk}`);
  },

  'reader: a token much longer than one read'() {
    const lex = grammar(source(long + ' end', 3), Lexer.LONGEST);
    const tok = lex.nextToken();

    eq(tok.type, 'ident');
    eq(tok.lexeme.length, 5001);
    eq(lex.nextToken().type, 'ws');
    eq(lex.nextToken().lexeme, 'end');
    eq(lex.nextToken(), null);
  },

  'reader: tokens keep their lexeme after the window moved on'() {
    const lex = grammar(source('x '.repeat(5000), 16), Lexer.LONGEST);
    const first = lex.nextToken();
    let tok;

    while((tok = lex.nextToken()) !== null);

    eq(first.lexeme, 'x');
    eq(`${first}`, 'x');
  },

  'reader: eof and size follow what has been read'() {
    const lex = grammar(source('ab', 1), Lexer.LONGEST);

    assert(!lex.eof);
    eq(lex.nextToken().lexeme, 'ab');
    eq(lex.nextToken(), null);
    assert(lex.eof);
    eq(lex.size, 2);
  },

  'reader: an unmatched character throws with its position'() {
    const lex = grammar(source('ab ?', 2), Lexer.LONGEST);
    let msg = '';

    try {
      while(lex.nextToken() !== null);
    } catch(e) {
      msg = e.message;
    }

    assert(/in\.txt:1:4: No matching token/.test(msg), msg);
  },

  'reader: a failing read throws'() {
    const lex = grammar({ read: () => -1 }, Lexer.LONGEST);
    let threw = false;

    try {
      lex.nextToken();
    } catch(e) {
      threw = true;
    }

    assert(threw);
  },

  'reader: character access needs an in-memory input'() {
    const lex = grammar(source('ab', 1), Lexer.LONGEST);
    let threw = false;

    try {
      lex.peekc();
    } catch(e) {
      threw = e instanceof TypeError;
    }

    assert(threw);
  },

  'reader: a std FILE'() {
    const file = '.tmp/test-lexer-reader.txt';

    try {
      mkdirSync('.tmp');
    } catch(e) {}

    writeFileSync(file, (TEXT + '\n').repeat(2000));

    const f = std.open(file, 'r');
    const want = scan(grammar(std.loadFile(file), Lexer.LONGEST));

    eq(scan(grammar(f, Lexer.LONGEST)), want);
    f.close();
  },

  'reader: setInput(reader) replaces the input'() {
    const lex = grammar('1', Lexer.LONGEST);

    lex.setInput(source('22 33', 2), 'r.txt');
    eq(scan(lex), 'number:22@1:1 ws: @1:3 number:33@1:4');
  },
});
