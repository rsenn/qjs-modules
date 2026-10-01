/* ---------- lib/parser.js: boost::spirit-style rule combinators ----------
 *
 * Rule/Terminal/OneOrMore/Optional/ZeroOrMore/Sequence/Expect all share the
 * combinator methods defined on Rule, so `.then()` (sequence), `.expect()`
 * (spirit's expectation operator `>`), `.or()` (alternative), `.some()`
 * (one-or-more), `.optional()` (zero-or-one) and `.many()` (zero-or-more /
 * Kleene star) compose regardless of which concrete subclass either operand
 * is. A bare number or plain function operand is wrapped like _char() /
 * run as a semantic action.
 *
 * _char()/stringInput() let these be exercised directly against a plain
 * character stream, without needing the token-oriented Lexer class
 * (quickjs-lexer.c) at all. */
import { _char, stringInput, Rule, Terminal, OneOrMore, Optional, ZeroOrMore, Sequence, Alternative, Expect, ExpectationError } from '../../lib/parser.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

function matches(rule, input) {
  const in_ = stringInput(input);
  const ok = rule.match(in_);

  return { ok, consumed: in_.charPos, eof: in_.eof };
}

function assertThrows(fn, msg) {
  try {
    fn();
  } catch(e) {
    return e;
  }
  throw new Error('assertThrows(): did not throw' + (msg ? ' - ' + msg : ''));
}

tests({
  /* ---------- .then() : sequence ---------- */
  '.then() matches two rules in order'() {
    const r = matches(_char('a').then(_char('b')), 'ab');

    assert(r.ok, 'should match');
    assert(r.eof, 'should consume the whole input');
  },

  '.then() fails when the second rule does not match'() {
    const r = matches(_char('a').then(_char('b')), 'ac');

    assert(!r.ok, 'should not match');
  },

  '.then() fails when the first rule does not match'() {
    const r = matches(_char('a').then(_char('b')), 'xb');

    assert(!r.ok, 'should not match');
  },

  '.then() backtracks the whole sequence on a later failure'() {
    const in_ = stringInput('axc');
    const rule = _char('a').then(_char('b')).then(_char('c'));

    assert(!rule.match(in_), 'should fail (b does not match x)');
    eq(0, in_.charPos); // rewound past a's own successful match too
  },

  '.then() chains flatten into one Sequence instead of nesting'() {
    const rule = _char('a').then(_char('b')).then(_char('c'));

    assert(rule instanceof Sequence);
    eq(3, rule.rules.length);
  },

  '.then() accepts several operands at once'() {
    const rule = _char('a').then(_char('b'), _char('c'));

    assert(rule instanceof Sequence);
    eq(3, rule.rules.length);
  },

  /* ---------- .expect() : expectation operator ---------- */
  '.expect() matches when both sides match'() {
    const r = matches(_char('a').expect(_char('b')), 'ab');

    assert(r.ok);
    assert(r.eof);
  },

  '.expect() builds an Expect, not a Sequence'() {
    const rule = _char('a').expect(_char('b'));

    assert(rule instanceof Expect);
    assert(!(rule instanceof Sequence));
  },

  '.expect() is a soft no-match (no throw) when the left side fails'() {
    const in_ = stringInput('xb');
    const rule = _char('a').expect(_char('b'));
    let result;

    result = rule.match(in_); // must not throw

    assert(!result, 'should not match');
    eq(0, in_.charPos); // backtracks, same as a failed .then()
  },

  '.expect() throws an ExpectationError (does not backtrack) when the right side fails'() {
    const in_ = stringInput('ax');
    const rule = _char('a').expect(_char('b'));

    assert(assertThrows(() => rule.match(in_)) instanceof ExpectationError);
    eq(1, in_.charPos); // NOT rewound - unlike a failed .then(), this is a hard error
  },

  'chained .expect() : failure of the first operand is still soft'() {
    const in_ = stringInput('xbc');
    const rule = _char('a').expect(_char('b')).expect(_char('c'));

    assert(!rule.match(in_)); // must not throw
  },

  'chained .expect() : failure of a middle operand still throws'() {
    const in_ = stringInput('axc');
    const rule = _char('a').expect(_char('b')).expect(_char('c'));

    assert(assertThrows(() => rule.match(in_)) instanceof ExpectationError);
  },

  'chained .expect() : failure of the last operand still throws'() {
    const in_ = stringInput('abx');
    const rule = _char('a').expect(_char('b')).expect(_char('c'));

    assert(assertThrows(() => rule.match(in_)) instanceof ExpectationError);
  },

  '(a.then(b)).expect(c) : a soft-sequence as the left side of an expectation'() {
    // a itself fails -> a.then(b) fails -> soft no-match for the whole thing
    const in_ = stringInput('xy');
    const rule = _char('a').then(_char('b')).expect(_char('c'));

    assert(!rule.match(in_)); // must not throw
  },

  '(a.then(b)).expect(c) : once a.then(b) succeeds, a failing c still throws'() {
    const in_ = stringInput('abx');
    const rule = _char('a').then(_char('b')).expect(_char('c'));

    assert(assertThrows(() => rule.match(in_)) instanceof ExpectationError);
  },

  /* ---------- .or() : alternative ---------- */
  '.or() builds an Alternative'() {
    assert(_char('a').or(_char('b')) instanceof Alternative);
  },

  '.or() matches the first branch that succeeds'() {
    const rule = _char('a').or(_char('b'));

    assert(matches(rule, 'a').ok);
    assert(matches(rule, 'b').ok);
    assert(!matches(rule, 'c').ok);
  },

  '.or() chains flatten into one Alternative instead of nesting'() {
    const rule = _char('a').or(_char('b')).or(_char('c'));

    assert(rule instanceof Alternative);
    eq(3, rule.rules.length);
  },

  /* ---------- .many() : zero-or-more (Kleene star) ---------- */
  '.many() builds a ZeroOrMore'() {
    assert(_char('a').many() instanceof ZeroOrMore);
  },

  '.many() greedily matches every occurrence'() {
    const in_ = stringInput('aaab');
    const rule = _char('a').many();

    assert(rule.match(in_));
    eq(3, in_.charPos);
  },

  '.many() succeeds on zero occurrences (unlike .some()/OneOrMore)'() {
    const in_ = stringInput('b');
    const rule = _char('a').many();

    assert(rule.match(in_));
    eq(0, in_.charPos);
  },

  '{ a* } matches both an empty and a populated block'() {
    const rule = () => _char('{').then(_char('a').many(), _char('}'));

    assert(matches(rule(), '{}').ok);
    assert(matches(rule(), '{aaa}').ok);
  },

  '.then() composes across Rule subclasses (Terminal, Sequence, ...)'() {
    const t = new Terminal('a', 'A');
    const rule = t.then(_char('b').then(_char('c'))); // Terminal then Sequence

    assert(rule instanceof Sequence);
    eq(3, rule.rules.length); // flattened, not nested

    const r = matches(rule, 'abc');
    assert(r.ok);
  },

  '.then() auto-wraps a bare number literal as a Rule'() {
    const rule = _char('a').then(5);

    eq(5, rule.rules[1].id);
    assert(rule.rules[1] instanceof Rule);
  },

  /* ---------- .some() : one-or-more ---------- */
  '.some() builds a OneOrMore'() {
    assert(_char('a').some() instanceof OneOrMore);
  },

  '.some() matches one occurrence'() {
    const r = matches(_char('a').some(), 'a');

    assert(r.ok);
    eq(1, r.consumed);
  },

  '.some() greedily matches every occurrence'() {
    const r = matches(_char('a').some(), 'aaab');

    assert(r.ok);
    eq(3, r.consumed); // stops right before the 'b'
  },

  '.some() fails (and backtracks) on zero occurrences'() {
    const in_ = stringInput('b');
    const rule = _char('a').some();

    assert(!rule.match(in_));
    eq(0, in_.charPos);
  },

  /* ---------- .optional() : zero-or-one-time ---------- */
  '.optional() builds an Optional'() {
    assert(_char('a').optional() instanceof Optional);
  },

  '.optional() matches when the sub-rule matches'() {
    const r = matches(_char('a').optional(), 'ab');

    assert(r.ok);
    eq(1, r.consumed);
  },

  '.optional() still succeeds when the sub-rule does not match (0 times)'() {
    const in_ = stringInput('b');
    const rule = _char('a').optional();

    assert(rule.match(in_), 'Optional never fails');
    eq(0, in_.charPos); // nothing consumed, but not a failure
  },

  /* ---------- semantic actions ---------- */
  'a plain function in a .then() chain runs as a semantic action'() {
    let called = 0;
    const rule = _char('{').then(function action() { called++; }, _char('}'));
    const r = matches(rule, '{}');

    assert(r.ok);
    eq(1, called);
  },

  'a semantic action receives the input/lexer it ran against'() {
    let seen;
    const rule = _char('{').then(in_ => { seen = in_; }, _char('}'));

    matches(rule, '{}');

    assert(seen && typeof seen.next == 'function', 'action was called with the input stream');
  },

  'an action returning false fails (and backtracks) the sequence'() {
    const in_ = stringInput('{}');
    const rule = _char('{').then(() => false, _char('}'));

    assert(!rule.match(in_), 'action returning false must fail the sequence');
    eq(0, in_.charPos);
  },

  'an action that does not return false does not affect the result'() {
    const rule = _char('{').then(() => 'anything, even falsy-looking strings are fine', _char('}'));
    const r = matches(rule, '{}');

    assert(r.ok);
  },

  /* ---------- putting it together: a small brace-block grammar ---------- */
  '{ a+ }  matches a brace block containing one-or-more "a"s'() {
    const rule = _char('{').then(_char('a').some(), _char('}'));
    const r = matches(rule, '{aaa}');

    assert(r.ok);
    assert(r.eof);
  },

  '{ a+ }  fails on an empty block (.some() requires at least one)'() {
    const r = matches(_char('{').then(_char('a').some(), _char('}')), '{}');

    assert(!r.ok);
  },

  '{ a? }  accepts either {} or {a} via the optional'() {
    const rule = () => _char('{').then(_char('a').optional(), _char('}'));

    assert(matches(rule(), '{}').ok);
    assert(matches(rule(), '{a}').ok);
  },

  'brace block with a semantic action counting each repetition'() {
    let count = 0;
    const item = _char('a').then(() => {
      count++;
    });
    const rule = _char('{').then(item.some(), _char('}'));
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
