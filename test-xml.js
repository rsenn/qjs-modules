import { read, write } from 'xml.so';
import ConsoleSetup from '../../lib/consoleSetup.js';
import PortableFileSystem from '../../lib/filesystem.js';
import { toXML } from '../../lib/json.js';
import Util from '../../lib/util.js';
import path from '../../lib/path.js';

async function main(...args) {
  await ConsoleSetup({
    depth: 5,
    colors: true,
    breakLength: 80,
    maxArrayLength: 10,
    maxStringLength: 30,
    compact: 3
  });
  await PortableFileSystem();
  let file = args[0] ?? 'C Theme 2.tmTheme';

  let base = path.basename(file, /\.[^.]*$/);

  let data = filesystem.readFile(file, 'utf-8');
  console.log('data:', Util.abbreviate(Util.escape(data)));

  // let result = parse2(Util.bufferToString(data));
  let result = read(data);

  console.log('result:', result);

  /* let xml = toXML(result, { depth: Infinity, quote: '"', indent: '' });
  console.log('xml:', xml);*/
  let str = toXML(result);
  console.log('write:', str);

  console.log(`Writing '${base + '.xml'}'...`);
  filesystem.writeFile(base + '.xml', str);

  await import('std').then(std => std.gc());
}

Util.callMain(main, true);
