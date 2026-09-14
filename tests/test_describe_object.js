import { describeObject } from '../lib/describe-object.js';
import { assert, eq, tests } from './tinytest.js';

class Base {
  static baseStatic() {}
  baseMethod() {}
}

class Foo extends Base {
  static bar() {}
  get y() {
    return 1;
  }
  set y(v) {}
  baz(a, b) {
    return a + b;
  }
}

tests({
  'throws on primitives'() {
    let threw = false;
    try {
      describeObject(1);
    } catch(e) {
      threw = e instanceof TypeError;
    }
    assert(threw, 'expected TypeError for a primitive');
  },
  'plain object'() {
    const d = describeObject({ a: 1, m() {} });
    eq(d.name, 'Object');
    eq(d.type, 'object');
    eq(d.fields.length, 1);
    eq(d.fields[0].name, 'a');
    eq(d.fields[0].value, 1);
    eq(d.methods.length, 1);
    eq(d.methods[0].name, 'm');
    eq(d.prototypeChain.length, 0);
  },
  'class instance'() {
    const d = describeObject(new Foo());
    eq(d.name, 'Foo');
    eq(d.type, 'object');
    // own members of a plain instance are empty; everything lives on the prototype chain
    eq(d.methods.length, 0);
    eq(d.prototypeChain.length, 2);
    eq(d.prototypeChain[0].constructorName, 'Foo');
    eq(d.prototypeChain[0].methods.length, 1);
    eq(d.prototypeChain[0].methods[0].name, 'baz');
    eq(d.prototypeChain[0].methods[0].arity, 2);
    eq(d.prototypeChain[0].getters.join(','), 'y');
    eq(d.prototypeChain[0].setters.join(','), 'y');
    eq(d.prototypeChain[1].constructorName, 'Base');
    eq(d.prototypeChain[1].methods.length, 1);
    eq(d.prototypeChain[1].methods[0].name, 'baseMethod');
  },
  'class/constructor function'() {
    const d = describeObject(Foo);
    eq(d.name, 'Foo');
    eq(d.type, 'function');
    eq(d.methods.length, 1);
    eq(d.methods[0].name, 'bar');
    assert(Array.isArray(d.constructorParams));
  },
  'symbol-keyed member'() {
    const sym = Symbol('custom');
    const d = describeObject({ [Symbol.iterator]() {}, [sym]: 42 });
    const methodNames = d.methods.map(m => m.name);
    const fieldNames = d.fields.map(f => f.name);
    assert(methodNames.includes('Symbol.iterator'));
    assert(fieldNames.includes(sym.toString()));
  },
});
