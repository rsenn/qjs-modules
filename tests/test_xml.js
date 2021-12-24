import * as os from 'os';
import * as std from 'std';
import { escape, quote, isObject } from 'util';
import inspect from 'inspect';
import * as xml from 'xml';
import * as path from 'path';
import Console from '../lib/console.js';

('use strict');
('use math');

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: 10,
      stringBreakNewline: false,
      maxArrayLength: 10,
      compact: 2,
      maxStringLength: 60
    }
  });

  let file = args[0] ?? '/etc/fonts/fonts.conf';

  let base = path.basename(file, path.extname(file));

  let data = std.loadFile(file, 'utf-8');

  let start = Date.now();

  let result = xml.read(data, file, true);
  let end = Date.now();
  console.log('result[0]', inspect(result[0], { depth: Infinity, compact: 1, maxArrayLength: Infinity }));

  console.log(`Parsing took ${end - start}ms`);

  if(/NETSCAPE-Bookmark-file-1/i.test(result[0].tagName)) {
    let tag,
      group,
      elements = [],
      links = [],
      obj = {},
      str = '';
    for(let element of result) {
      if(isObject(element) && element.tagName) {
        tag = element.tagName;
        str = '';
      } else if(typeof element == 'string') {
        str += (str.length ? ' ' : '') + element;
      }
      if(/^\/h3$/i.test(tag)) group = str;
      else if(/^(a)$/i.test(tag)) {
        if(isObject(element.attributes)) {
          obj.href = element.attributes['HREF'];
          obj.date = new Date(+element.attributes['ADD_DATE'] * 1000);
        }
        if(group) obj.group = group;
      }
      if(/^\/(a)$/i.test(tag)) {
        if(str) obj.str = str;

        elements.push(obj);
        obj = {};
      }
    }
    result = [];

    elements.sort((a, b) => a.date - b.date);

    for(let element of elements) {
      const { href, date, group } = element;
      result.push({ tagName: 'a', attributes: { href, date } });
      if(isObject(element) && /^a$/i.test(element.tagName)) {
        const add_date = new Date(+element.attributes['ADD_DATE'] * 1000);
        const href = element.attributes['HREF'];
        console.log('a', { href, add_date });
      }
    }
  }

  // console.log('result:', inspect(result, { depth: Infinity, compact: 1, maxArrayLength: Infinity }));
  WriteFile(base + '.json', JSON.stringify(result, null, 2));

  start = Date.now();
  let str = xml.write(result);
  end = Date.now();

  console.log(`Generating took ${end - start}ms`);

  WriteFile(base + '.xml', str);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
