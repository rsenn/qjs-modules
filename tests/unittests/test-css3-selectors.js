import { AttributeSelector, ClassSelector, IdSelector, parseSelectors, TypeSelector } from '../../lib/css3-selectors.js';
import { Parser } from '../../lib/dom.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const el = (tagName, attributes = {}) => ({ tagName, attributes });
const parse = html => new Parser().parseFromString(html);
const HTML = '<html><body><div id="a" class="x y"><span class="icon">s</span><p>t</p></div><div class="x"/></body></html>';

tests({
  'TypeSelector matches the tag name case-insensitively'() {
    const sel = new TypeSelector('html');

    assert(sel(el('HTML')), 'HTML');
    assert(sel(el('html')), 'html');
    assert(!sel(el('DIV')), 'DIV');
  },

  'ClassSelector matches one class of several'() {
    const sel = new ClassSelector('common');

    assert(sel(el('A', { class: 'common big item' })), 'in list');
    assert(!sel(el('A', { class: 'other' })), 'absent');
    assert(!sel(el('A', { class: 'commonplace' })), 'prefix only');
  },

  'IdSelector matches the id attribute'() {
    assert(new IdSelector('a')(el('A', { id: 'a' })), 'match');
    assert(!new IdSelector('a')(el('A', { id: 'b' })), 'mismatch');
  },

  'AttributeSelector with only a name tests presence'() {
    assert(new AttributeSelector('n')(el('A', { n: 'x' })), 'present');
    assert(!new AttributeSelector('n')(el('A')), 'absent');
  },

  'AttributeSelector operators'() {
    const cases = [
      ['=', 'ab', 'ab', true],
      ['=', 'ab', 'abc', false],
      ['~=', 'b', 'a b c', true],
      ['~=', 'b', 'abc', false],
      ['^=', 'ab', 'abc', true],
      ['^=', 'bc', 'abc', false],
      ['$=', 'bc', 'abc', true],
      ['$=', 'ab', 'abc', false],
      ['*=', 'b', 'abc', true],
      ['*=', 'z', 'abc', false],
      ['|=', 'en', 'en-US', true],
      ['|=', 'en', 'english', false],
    ];

    for(const [op, value, attr, expected] of cases) eq(expected, !!new AttributeSelector('n', value, op)(el('A', { n: attr })));
  },

  'AttributeSelector.toSource() is a function source'() {
    assert(/name == 'test'/.test(new AttributeSelector('name', 'test').toSource()), 'source');
  },

  'parseSelectors yields one predicate per comma-separated selector'() {
    eq(1, [...parseSelectors('div.x')].length);
    eq(2, [...parseSelectors('a, b')].length);
  },

  'parseSelectors predicates match elements'() {
    const [sel] = parseSelectors('element.big[name="test"]');

    assert(sel(el('element', { class: 'big', name: 'test' })), 'match');
    assert(!sel(el('element', { class: 'big', name: 'other' })), 'wrong attribute');
    assert(!sel(el('other', { class: 'big', name: 'test' })), 'wrong tag');
  },

  'parseSelectors handles an attribute-only selector'() {
    const [sel] = parseSelectors('[name="C1"]');

    assert(sel(el('X', { name: 'C1' })), 'match');
    assert(!sel(el('X', { name: 'C2' })), 'mismatch');
  },

  'querySelector with type, class and id'() {
    const doc = parse(HTML);

    eq('p', doc.querySelector('p').tagName.toLowerCase());
    eq('span', doc.querySelector('span.icon').tagName.toLowerCase());
    eq('div', doc.querySelector('#a').tagName.toLowerCase());
    eq('a', doc.querySelector('div.x.y').getAttribute('id'));
  },

  'querySelector with a descendant combinator'() {
    eq('s', parse(HTML).querySelector('div .icon').textContent);
  },

  'querySelector with an attribute selector'() {
    eq('div', parse(HTML).querySelector('[id=a]').tagName.toLowerCase());
  },

  'querySelector finds nothing'() {
    eq(undefined, parse(HTML).querySelector('.nope'));
  },

  'querySelectorAll collects every match'() {
    const doc = parse(HTML);

    eq(2, [...doc.querySelectorAll('.x')].length);
    eq(1, [...doc.querySelectorAll('div > p')].length);
    eq(3, [...doc.querySelectorAll('div, span')].length);
  },
});
