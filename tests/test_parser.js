import { readFileSync, writeFileSync } from 'fs';
import * as path from 'path';
import EBNFParser from '../lib/parser/ebnf.js';
import { Console } from 'console';
import extendArray from 'extendArray';
import { Lexer } from 'lexer';
//import inspect from 'inspect';
extendArray(Array.prototype);

let code = 'C';
Error.stackTraceLimit = Infinity;

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({ start, pos, size })}`;
}

function InstanceOf(obj, ctor) {
  return typeof obj == 'object' && obj != null && obj instanceof ctor;
}

function IsRegExp(regexp) {
  return InstanceOf(regexp, RegExp);
}

function RegExpToArray(regexp) {
  //console.log("RegExpToArray", regexp);
  const { source, flags } = regexp;
  return [Lexer.unescape(source), flags];
}

function LoadScript(file) {
  let code = readFileSync(file, 'utf-8');
  //console.log('LoadScript', { code });
  return std.evalScript(code, {});
}

function WriteObject(file, obj, fn = arg => arg) {
  return writeFileSync(
    file,
    fn(
      inspect(obj, {
        colors: false,
        reparseable: true,
        breakLength: 120,
        maxStringLength: Infinity,
        maxArrayLength: Infinity,
        compact: 3,
        multiline: true,
      }),
    ),
  );
}

function* Range(start, end) {
  for(let i = start | 0; i <= end; i++) yield i;
}

function* MatchAll(regexp, str) {
  let match;
  while((match = regexp.exec(str))) yield match;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 2,
      maxArrayLength: Infinity,
      maxStringLength: Infinity,
      breakLength: 80,
      compact: 2,
      showHidden: false,
      customInspect: true,
    },
  });

  console.log('console.options', console.options);

  let optind = 0;

  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) {
      code = globalThis.code = args[++optind].toUpperCase();
    }

    optind++;
  }
  let file = args[optind] ?? 'tests/Shell-Grammar.y';

  console.log('file', file);

  let parser = new EBNFParser(null);

  const buf = readFileSync(file, null);
  parser.lexer = new BNFLexer(buf, file);
  let grammar = parser.parse();

  Object.assign(globalThis, { buf, file, parser, grammar });

  // parser.setInput();

  /*
  let outputFile = args[optind + 1] ?? 'grammar.kison';
  console.log('file:', file);
  let str = std.loadFile(file, 'utf-8');
  console.log('str:', str.slice(0, 50) + '...');
  let len = str.length;
  let type = path.extname(file).substring(1);

  let grammar = null; //LoadScript(outputFile);

  let parser = new EBNFParser(grammar);

  parser.setInput(str, file);

  grammar = parser.parse();
  if(grammar) {
    WriteObject('grammar.kison', grammar, str => `(function () {\n    return ` + str.replace(/\n/g, '\n    ') + `;\n\n})();`);
    //  console.log('grammar:', grammar);
  }
  std.gc();
  return !!grammar;*/
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}

/* ---------- lib/parser.js: boost::spirit-style operator overloading ----------
 *
 * Rule/Terminal/OneOrMore/Optional/Sequence all share one [Symbol.operatorSet]
 * (installed on Rule.prototype, inherited down the prototype chain), so `>>`
 * (sequence, also usable as `<<` - spirit's own karma/generator-side spelling
 * of the same idea), unary `+` (one-or-more), and unary `-` (optional /
 * "zero-or-one-time") all compose regardless of which concrete subclass
 * either operand is. See doc/predicate.md for how the underlying
 * Operators.create()/cross-type dispatch mechanism works in general, and the
 * comments in lib/parser.js itself for the specifics of this module's use of
 * it (in particular why Number, but not String, can be mixed into a `>>`
 * chain as a bare literal).
 *
 * _char()/stringInput() let these be exercised directly against a plain
 * character stream, without needing the token-oriented Lexer class
 * (quickjs-lexer.c) at all. */
import { _char, stringInput, Rule, Terminal, OneOrMore, Optional, Sequence } from '../lib/parser.js';
import { assert, eq, tests } from './tinytest.js';

function matches(rule, input) {
  const in_ = stringInput(input);
  const ok = rule.match(in_);

  return { ok, consumed: in_.charPos, eof: in_.eof };
}

tests({
  /* ---------- >> : sequence ---------- */
  '>> matches two rules in order'() {
    const r = matches(_char('a') >> _char('b'), 'ab');

    assert(r.ok, 'should match');
    assert(r.eof, 'should consume the whole input');
  },

  '>> fails when the second rule does not match'() {
    const r = matches(_char('a') >> _char('b'), 'ac');

    assert(!r.ok, 'should not match');
  },

  '>> fails when the first rule does not match'() {
    const r = matches(_char('a') >> _char('b'), 'xb');

    assert(!r.ok, 'should not match');
  },

  '>> backtracks the whole sequence on a later failure'() {
    const in_ = stringInput('axc');
    const rule = _char('a') >> _char('b') >> _char('c');

    assert(!rule.match(in_), 'should fail (b does not match x)');
    eq(0, in_.charPos); // rewound past a's own successful match too
  },

  '>> chains flatten into one Sequence instead of nesting'() {
    const rule = _char('a') >> _char('b') >> _char('c');

    assert(rule instanceof Sequence);
    eq(3, rule.rules.length);
  },

  '<< is spirit-karma-style sequence, same as >>'() {
    const r = matches(_char('a') << _char('b') << _char('c'), 'abc');

    assert(r.ok);
    assert(r.eof);
  },

  '>> composes across Rule subclasses (Terminal, Sequence, ...)'() {
    const t = new Terminal('a', 'A');
    const rule = t >> (_char('b') >> _char('c')); // Terminal >> Sequence

    assert(rule instanceof Sequence);
    eq(3, rule.rules.length); // flattened, not nested

    const r = matches(rule, 'abc');
    assert(r.ok);
  },

  '>> auto-wraps a bare number literal as a Rule'() {
    const rule = _char('a') >> 5;

    eq(5, rule.rules[1].id);
    assert(rule.rules[1] instanceof Rule);
  },

  /* ---------- unary + : one-or-more ---------- */
  'unary + builds a OneOrMore'() {
    assert(+_char('a') instanceof OneOrMore);
  },

  'unary + matches one occurrence'() {
    const r = matches(+_char('a'), 'a');

    assert(r.ok);
    eq(1, r.consumed);
  },

  'unary + greedily matches every occurrence'() {
    const r = matches(+_char('a'), 'aaab');

    assert(r.ok);
    eq(3, r.consumed); // stops right before the 'b'
  },

  'unary + fails (and backtracks) on zero occurrences'() {
    const in_ = stringInput('b');
    const rule = +_char('a');

    assert(!rule.match(in_));
    eq(0, in_.charPos);
  },

  /* ---------- unary - : zero-or-one-time (optional) ---------- */
  'unary - builds an Optional'() {
    assert(-_char('a') instanceof Optional);
  },

  'unary - matches when the sub-rule matches'() {
    const r = matches(-_char('a'), 'ab');

    assert(r.ok);
    eq(1, r.consumed);
  },

  'unary - still succeeds when the sub-rule does not match (0 times)'() {
    const in_ = stringInput('b');
    const rule = -_char('a');

    assert(rule.match(in_), 'Optional never fails');
    eq(0, in_.charPos); // nothing consumed, but not a failure
  },

  /* ---------- semantic actions ---------- */
  'a plain function in a >> chain runs as a semantic action'() {
    let called = 0;
    const rule = _char('{') >> function action() { called++; } >> _char('}');
    const r = matches(rule, '{}');

    assert(r.ok);
    eq(1, called);
  },

  'a semantic action receives the input/lexer it ran against'() {
    let seen;
    const rule = _char('{') >> (in_ => { seen = in_; }) >> _char('}');

    matches(rule, '{}');

    assert(seen && typeof seen.next == 'function', 'action was called with the input stream');
  },

  'an action returning false fails (and backtracks) the sequence'() {
    const in_ = stringInput('{}');
    const rule = _char('{') >> (() => false) >> _char('}');

    assert(!rule.match(in_), 'action returning false must fail the sequence');
    eq(0, in_.charPos);
  },

  'an action that does not return false does not affect the result'() {
    const rule = _char('{') >> (() => 'anything, even falsy-looking strings are fine') >> _char('}');
    const r = matches(rule, '{}');

    assert(r.ok);
  },

  /* ---------- putting it together: a small brace-block grammar ---------- */
  '{ +a }  matches a brace block containing one-or-more "a"s'() {
    const rule = _char('{') >> +_char('a') >> _char('}');
    const r = matches(rule, '{aaa}');

    assert(r.ok);
    assert(r.eof);
  },

  '{ +a }  fails on an empty block (+ requires at least one)'() {
    const r = matches(_char('{') >> +_char('a') >> _char('}'), '{}');

    assert(!r.ok);
  },

  '{ -a }  accepts either {} or {a} via the optional'() {
    const rule = () => _char('{') >> -_char('a') >> _char('}');

    assert(matches(rule(), '{}').ok);
    assert(matches(rule(), '{a}').ok);
  },

  'brace block with a semantic action counting each repetition'() {
    let count = 0;
    const item = _char('a') >> (() => {
      count++;
    });
    const rule = _char('{') >> +item >> _char('}');
    const r = matches(rule, '{aaaa}');

    assert(r.ok);
    eq(4, count);
  },

  /* ---------- stringInput() ---------- */
  'stringInput() tracks position and end-of-input'() {
    const in_ = stringInput('ab');

    assert(!in_.eof);
    eq('a', in_.next());
    eq('b', in_.next());
    assert(in_.eof);
  },
});