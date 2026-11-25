// @portable
/* lib/terminal.js: ScrollView source text mode */

import { ScrollView } from '../../lib/terminal/scrollview.js';
import { scrollbar } from '../../lib/terminal/widgets.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const same = (a, b) => eq(JSON.stringify(a), JSON.stringify(b));

tests({
  'old-style ScrollView still works'() {
    const v = new ScrollView(3, ['a', 'b', 'c', 'd', 'e']);

    same(['c', 'd', 'e'], v.visible());
    assert(v.handleKey({ name: 'pageup' }));
    same(['a', 'b', 'c'], v.visible());
    v.push('f');
    same(['a', 'b', 'c'], v.visible());
    v.handleKey({ type: 'mouse', button: 'wheeldown' });
    same(['d', 'e', 'f'], v.visible());
    assert(v.follow);
    same(['d ┃', 'e ┃', 'f ┃'].length, v.render(3).length);
    same(['│', '│', '┃', '┃'], scrollbar(4, 8, 4));
  },

  'setWidth(20), pushText(100-char para + short one): rewraps == 2'() {
    const v = new ScrollView(10);
    v.setWidth(20);
    // Two entries: a long paragraph and a short one
    v.pushText(
      'This is a hundred character paragraph that will wrap at width twenty cells and contains multiple words and spaces',
      'Short'
    );
    eq(v.rewraps, 2);
    // Each line should be <= 20 cells
    v.lines.forEach(l => assert(l.length <= 20, `line too long: ${l.length}`));
  },

  'pushText one more entry: rewraps == 3'() {
    const v = new ScrollView(10);
    v.setWidth(20);
    v.pushText(
      'This is a hundred character paragraph that will wrap at width twenty cells and contains multiple words and spaces',
      'Short'
    );
    v.pushText('Another entry');
    eq(v.rewraps, 3);
  },

  'setWidth(20) again: rewraps still 3, lines unchanged'() {
    const v = new ScrollView(10);
    v.setWidth(20);
    v.pushText(
      'This is a hundred character paragraph that will wrap at width twenty cells and contains multiple words and spaces',
      'Short'
    );
    v.pushText('Another entry');
    const beforeLines = v.lines.length;
    v.setWidth(20); // same width
    eq(v.rewraps, 3);
    eq(v.lines.length, beforeLines);
  },

  'setWidth(10): rewraps == 6, all lines <= 10 cells wide'() {
    const v = new ScrollView(10);
    v.setWidth(20);
    v.pushText(
      'This is a hundred character paragraph that will wrap at width twenty cells and contains multiple words and spaces',
      'Short'
    );
    v.pushText('Another entry');
    eq(v.rewraps, 3);
    v.setWidth(10);
    eq(v.rewraps, 6); // 3 entries * 2 rewraps each (was wrapped at 20, now re-wrapped at 10)
    // All lines should be <= 10 cells
    v.lines.forEach(l => assert(l.length <= 10, `line too long: ${l.length} > 10`));
  },

  'follow stays on after resize if at end, off if scrolled up'() {
    const v = new ScrollView(3, ['a', 'b', 'c', 'd', 'e']);
    v.setWidth(20);
    // At bottom initially
    assert(v.atBottom);
    assert(v.follow);
    v.pushText('new entry');
    assert(v.atBottom);
    assert(v.follow);
    // Scroll up
    v.scrollTo(0);
    assert(!v.atBottom);
    assert(!v.follow);
  },

  'pushText before setWidth throws'() {
    const v = new ScrollView(5);
    try {
      v.pushText('test');
      assert(false, 'should have thrown');
    } catch(e) {
      assert(e.message.includes('setWidth must be called before pushText'));
    }
  },

  'get width() returns wrap width or undefined'() {
    const v = new ScrollView(5);
    // Before setWidth, width should be undefined
    eq(v.width, undefined);
    // After setWidth, width should be the set value
    v.setWidth(20);
    eq(v.width, 20);
    // Can change width
    v.setWidth(15);
    eq(v.width, 15);
  },

  'setText replaces all source entries'() {
    const v = new ScrollView(5);
    v.setWidth(10);
    v.setText(['entry one', 'entry two', 'entry three']);
    eq(v.rewraps, 3); // 3 source entries
    // Display lines may be more due to wrapping, but all should be <= 10 cells
    assert(v.lines.length >= 3);
    v.lines.forEach(l => assert(l.length <= 10, `line too long: ${l.length} > 10`));
  },

  'width getter returns current wrap width'() {
    const v = new ScrollView(5);
    // Before setWidth, width should be undefined
    eq(v.width, undefined);
    // After setWidth, width should be the set value
    v.setWidth(20);
    eq(v.width, 20);
    // Can change width
    v.setWidth(15);
    eq(v.width, 15);
  }
});
