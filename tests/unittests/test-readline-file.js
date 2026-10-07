/* readline.createInterface() on a FILE (the fd path `process.stdin` takes): qjsm only */
import * as readline from 'readline';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const withFile = (text, fn) => {
  const path = `/tmp/test-readline-file-${std.getenv('USER') ?? 'u'}-${Math.random().toString(36).slice(2)}.txt`;
  const out = std.open(path, 'w');

  out.puts(text);
  out.close();

  const file = std.open(path, 'r');

  return fn(file).finally(() => {
    file.close();
    os.remove(path);
  });
};

const all = async input => {
  const got = [];

  for await(const l of readline.createInterface({ input })) got.push(l);

  return got;
};

tests({
  async 'reads the lines of a FILE'() {
    eq((await withFile('a\nb\r\n€uro\nlast', all)).join('|'), 'a|b|€uro|last');
  },
  async 'a long line and a multi-byte char across the read size'() {
    const got = await withFile('x'.repeat(100000) + '\n' + 'y'.repeat(70000) + 'é\nend\n', all);

    eq(got.map(l => l.length).join(), '100000,70001,3');
    eq(got[1].slice(-1), 'é');
  },
  async 'an empty FILE closes without a line'() {
    eq((await withFile('', all)).length, 0);
  },
  async 'pause stops the reads until resume'() {
    await withFile('1\n2\n3\n', async file => {
      const rl = readline.createInterface({ input: file });
      const got = [];

      rl.pause();
      rl.on('line', l => got.push(l));
      await new Promise(r => setTimeout(r, 30));
      eq(got.length, 0);
      rl.resume();
      await new Promise(r => rl.on('close', r));
      eq(got.join(), '1,2,3');
    });
  },
});
