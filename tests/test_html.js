import { HTMLParser, streamSrcHrefAndText } from 'html';
import { assert, eq, tests } from './tinytest.js';

function collect(html) {
  const p = new HTMLParser(html);
  const events = [];
  let e;
  while((e = p.parse()) !== null) events.push(e);
  return events;
}

await tests({
  'constructs without a second argument'() {
    const p = new HTMLParser('<a></a>');
    assert(p.parse() !== null);
  },

  'accepts a filename string as the second argument'() {
    const p = new HTMLParser('<a></a>', 'test.html');
    assert(p.parse() !== null);
  },

  'accepts an options object as the second argument'() {
    const p = new HTMLParser('<a></a>', { filename: 'test.html' });
    assert(p.parse() !== null);
  },

  'lowercases tag and attribute names'() {
    const events = collect('<DIV CLASS="x"></DIV>');
    eq('div', events[0].name);
    eq('class', events[1].name);
    eq('/div', events[2].name);
  },

  '<script> content is never tokenized as markup'() {
    const events = collect('<script>if(a<b){c()}</script>');
    eq('script', events[0].name);
    eq('if(a<b){c()}', events[1].value);
    eq('/script', events[2].name);
  },

  '<style> content is never tokenized as markup'() {
    const events = collect('<style>a<b{color:red}</style>');
    eq('style', events[0].name);
    eq('a<b{color:red}', events[1].value);
    eq('/style', events[2].name);
  },

  '<textarea> content is never tokenized as markup'() {
    const events = collect('<textarea>1<2</textarea>');
    eq('textarea', events[0].name);
    eq('1<2', events[1].value);
    eq('/textarea', events[2].name);
  },

  '<title> content is never tokenized as markup'() {
    const events = collect('<title>a<b</title>');
    eq('title', events[0].name);
    eq('a<b', events[1].value);
    eq('/title', events[2].name);
  },

  'tolerates a mismatched closing tag instead of throwing'() {
    const events = collect('<div><p>text</div>');
    assert(events.length > 0, 'expected recovery, not a thrown error');
  },

  'streamSrcHrefAndText() yields src/href attribute values'() {
    const results = [...streamSrcHrefAndText('<img src="a.png"><a href="b.html">text</a>', {})];
    const types = results.map(r => r.type);
    assert(types.includes('src'));
    assert(types.includes('href'));
  },
});
