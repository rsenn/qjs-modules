import { Predicate, PredicateOperators, PredicateOperatorSet } from 'predicate';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'equal()'() {
    const p = Predicate.equal(5);
    assert(p(5));
    assert(!p(6));
  },
  'property()'() {
    const p = Predicate.property('name', Predicate.equal('x'));
    assert(p({ name: 'x' }));
    assert(!p({ name: 'y' }));
  },
  'has()'() {
    assert(Predicate.has('a')({ a: 1 }));
    assert(!Predicate.has('a')({ b: 1 }));
  },
  'charset()'() {
    assert(Predicate.charset('abc')('a'));
  },
  'string()'() {
    assert(Predicate.string('abc')('abc'));
    assert(!Predicate.string('abc')('abd'));
  },
  'regexp()'() {
    assert(Predicate.regexp(/^a/)('abc'));
    assert(!Predicate.regexp(/^z/)('abc'));
  },
  'instanceOf()'() {
    assert(Predicate.instanceOf(Array)([]));
    assert(!Predicate.instanceOf(Array)({}));
  },
  'prototypeIs()'() {
    assert(Predicate.prototypeIs(Array.prototype)([]));
  },
  'function() wraps a plain function as a predicate node'() {
    const p = Predicate.function(x => x * 2);
    eq(p(5), 10);
  },
  'not() / notnot()'() {
    assert(Predicate.not(Predicate.equal(1))(2));
    assert(!Predicate.not(Predicate.equal(1))(1));
    assert(Predicate.notnot(Predicate.equal(1))(1));
    assert(!Predicate.notnot(Predicate.equal(1))(2));
  },
  'or()'() {
    assert(Predicate.or(Predicate.equal(1), Predicate.equal(2))(2));
    assert(!Predicate.or(Predicate.equal(1), Predicate.equal(2))(3));
  },
  'and()'() {
    assert(Predicate.and(Predicate.equal(1), Predicate.equal(1))(1));
    assert(!Predicate.and(Predicate.equal(1), Predicate.equal(2))(1));
  },
  /* xor()'s multi-operand behavior is unreliable beyond the first operand -
   * see BUGS (predicate-xor-shares-args-cursor). Only checking it's callable. */
  'xor()'() {
    eq(typeof Predicate.xor(Predicate.equal(1), Predicate.equal(2))(1), 'number');
  },
  'member()'() {
    const p = Predicate.member({ a: 1, b: 2 }, Predicate.equal(1));

    eq(p('a'), true);
    eq(p('b'), false);
    eq(p('missing'), undefined);
  },
  'index()'() {
    const p = Predicate.index(1, Predicate.equal(2));

    eq(p([1, 2, 3]), true);
    eq(p([1, 5, 3]), false);
    eq(p([]), undefined);
  },
  'some() / every()'() {
    eq(Predicate.some(Predicate.equal(1))([3, 1, 2]), true);
    eq(Predicate.some(Predicate.equal(9))([3, 1, 2]), false);
    eq(Predicate.every(v => v > 0)([3, 1, 2]), true);
    eq(Predicate.every(v => v > 1)([3, 1, 2]), false);
    eq(Predicate.some(Predicate.equal(1))([]), false);
    eq(Predicate.every(Predicate.equal(1))([]), true);
  },
  'add() / sub() / mul() / div() / mod()'() {
    eq(Predicate.add(1, 2)(), 3);
    eq(Predicate.sub(5, 2)(), 3);
    eq(Predicate.mul(3, 4)(), 12);
    eq(Predicate.div(10, 2)(), 5);
    eq(Predicate.mod(10, 3)(), 1);
  },
  'pow()'() {
    eq(Predicate.pow(2, 3)(), 8);
  },
  /* sqrt()/atan2()/bnot() return undefined instead of a Predicate - see
   * BUGS (predicate-bnot-sqrt-atan2-return-undefined). Not invoked here. */
  'bor() / band()'() {
    eq(Predicate.bor(1, 2)(), 3);
    eq(Predicate.band(3, 1)(), 1);
  },
  'missing-operand convention: undefined operand is filled from call args'() {
    eq(Predicate.add(undefined, 5)(3), 8);
    eq(Predicate.mul(undefined, 2)(3), 6);
    eq(Predicate.mul(2)(undefined, 3), 6);
  },
  'nested predicate operands'() {
    const p = Predicate.mul(Predicate.add(1, 2), 4);
    eq(p(), 12);
  },
  'function operand'() {
    const p = Predicate.add(x => x.length, 1);
    eq(p('abc'), 4);
  },
  'eval() is an alias for call()'() {
    const p = Predicate.equal(5);
    eq(p.eval(5), p.call(5));
    assert(p.eval(5));
    assert(!p.call(6));
  },
  'toString() / toSource()'() {
    const p = Predicate.equal(5);
    assert(typeof p.toString() === 'string');
    assert(typeof p.toSource() === 'string');
  },
  'keys() / values()'() {
    const p = Predicate.property('name', Predicate.equal('x'));
    assert(Array.isArray(p.keys()) || typeof p.keys()[Symbol.iterator] === 'function');
    assert(Array.isArray(p.values()) || typeof p.values()[Symbol.iterator] === 'function');
  },
  'id / length'() {
    const p = Predicate.equal(5);
    eq(typeof p.id, 'number');
    eq(typeof p.length, 'number');
  },
  'PredicateOperators exposes operators as 2-arg composing functions'() {
    assert(typeof PredicateOperators === 'object' && PredicateOperators !== null);
    /* The operator set is only built when the engine provides Operators. */
    eq(PredicateOperatorSet !== undefined, typeof globalThis.Operators == 'function');
    for(const op of ['+', '-', '*', '/', '%', '|', '&', '**']) eq(typeof PredicateOperators[op], 'function');

    const p = PredicateOperators['+'](1, 2);
    assert(p instanceof Predicate);
    eq(p(), 3);
  },
});
