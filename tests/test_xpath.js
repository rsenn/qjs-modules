import { XPath, ImmutableXPath, MutableXPath, XPathExpression, XPathEvaluator, XPathResult, XPathException, DereferenceError, parseXPath, getSiblings, buildXPath } from 'xpath';
import { assert, eq, tests } from './tinytest.js';

function tree() {
  return {
    tagName: 'html',
    children: [
      { tagName: 'head', children: [] },
      {
        tagName: 'body',
        children: [
          { tagName: 'div', children: [{ tagName: 'span', children: [] }] },
          { tagName: 'div', children: [] },
          { tagName: 'p', children: [] },
        ],
      },
    ],
  };
}

tests({
  'new XPath() - string/array/leading-slash forms'() {
    eq(new XPath('body/div[1]').length, 2);
    eq(new XPath(['body', 'div[1]']).length, 2);
    eq(new XPath('/body/div[1]').length, 2); // leading '/' is dropped
    eq(new XPath('head').length, 1);
  },

  'XPath is iterable'() {
    const xp = new XPath('body/div[1]');
    eq([...xp].length, 2);
    eq(Object.prototype.toString.call(xp), '[object XPath]');
  },

  'deref() - single and nested tag-name segments'() {
    const t = tree();

    eq(new XPath('head').deref(t).tagName, 'head');
    eq(new XPath('body').deref(t).tagName, 'body');
    eq(new XPath('body/div[1]/span').deref(t).tagName, 'span');
  },

  'deref() - tagName[N] disambiguates same-named siblings'() {
    const t = tree();

    const first = new XPath('body/div[1]').deref(t);
    const second = new XPath('body/div[2]').deref(t);

    assert(first === t.children[1].children[0]);
    assert(second === t.children[1].children[1]);
  },

  "deref() - plain 'children'/index steps (non tagName segments)"() {
    const t = tree();
    const xp = new XPath(['children', 1, 'children', 0]);

    assert(xp.deref(t) === t.children[1].children[0]);
  },

  'deref() - throws DereferenceError on missing tag'() {
    const t = tree();
    let threw = false;

    try {
      new XPath('body/table').deref(t);
    } catch(e) {
      threw = true;
      assert(e instanceof DereferenceError);
    }

    assert(threw);
  },

  'toPointer()'() {
    const t = tree();
    const xp = new XPath('body/div[2]');
    const parts = [...xp.toPointer(t)];

    eq(JSON.stringify(parts), JSON.stringify(['children', '1', 'children', '1']));
  },

  'parseXPath()'() {
    const t = tree();
    const parsed = parseXPath('body/div[1]');

    assert(parsed instanceof XPath);
    eq(parsed.deref(t).tagName, 'div');
  },

  'buildXPath()'() {
    const t = tree();

    const built1 = [...buildXPath(['children', 1, 'children', 0], t)].join('/');
    eq(built1, 'body/div[1]');

    const built2 = [...buildXPath(['children', 1, 'children', 1], t)].join('/');
    eq(built2, 'body/div[2]');

    // single, unambiguous tag names don't get a bracket index
    const built3 = [...buildXPath(['children', 0], t)].join('/');
    eq(built3, 'head');
  },

  'buildXPath() / XPath round-trip'() {
    const t = tree();
    const ptr = ['children', 1, 'children', 1];

    const built = [...buildXPath(ptr, t)].join('/');
    const node = new XPath(built).deref(t);

    let expected = t;
    for(const key of ptr) expected = expected[key];

    assert(node === expected);
  },

  'getSiblings()'() {
    const t = tree();
    const sibs = getSiblings(['children', 1, 'children', 0], t);

    eq(sibs.length, 3);
    eq(JSON.stringify(sibs.map(s => s.tagName)), JSON.stringify(['div', 'div', 'p']));
    assert(sibs[0] === t.children[1].children[0]);
  },

  'ImmutableXPath/MutableXPath/XPathExpression are XPath'() {
    eq(ImmutableXPath, XPath);
    eq(MutableXPath, XPath);
    eq(XPathExpression, XPath);
  },

  'XPathEvaluator/XPathResult/XPathException'() {
    const t = tree();
    const ev = new XPathEvaluator();
    const expr = ev.createExpression('body/div[1]');

    assert(expr instanceof XPath);
    eq(expr.deref(t).tagName, 'div');

    const res = new XPathResult({ booleanValue: true, resultType: 1 });
    eq(res.booleanValue, true);
    eq(res.resultType, 1);
    eq(res.numberValue, undefined);

    const exc = new XPathException(42);
    eq(exc.code, 42);
  },

  'DereferenceError'() {
    const e = new DereferenceError('boom');
    assert(e instanceof Error);
    eq(e.message, 'boom');
    eq(Object.prototype.toString.call(e), '[object DereferenceError]');
  },
});
