/* utilities/resolve-imports.js: bundling a file import, and --module-map (-M NAME=PATH),
 * which turns an import specifier like 'extendArray' into a file import so it is bundled. */
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const script = path.resolve(path.dirname(scriptArgs[0]), '../../utilities/resolve-imports.js');
const dir = fs.mkdtempSync(path.join(std.getenv('TMPDIR') || '/tmp', 'resolve-imports-'));
const out = path.join(dir, 'out.js');
const write = (name, text) => fs.writeFileSync(path.join(dir, name), text);
const read = name => fs.readFileSync(path.join(dir, name), 'utf-8');

/* runs from `dir`, so the relative paths in the arguments are relative to it;
 * os.exec rather than child_process: output goes to a log file */
function run(...args) {
  const log = path.join(dir, 'run.log');
  const fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  const status = os.exec([process.execPath, script, ...args], { cwd: dir, stdout: fd, stderr: fd, block: true });

  os.close(fd);

  return { status, stdout: read('run.log'), stderr: '' };
}

tests({
  'setup'() {
    write('helper.js', "import { isString } from 'util';\nexport const greet = n => 'hi ' + n;\n");
    write('main.js', "import { greet } from 'mylib';\nimport * as os from 'os';\nconsole.log(greet('x'));\n");
    write('sub.js', "import { greet } from 'pkg/helper.js';\nconsole.log(greet('y'));\n");
    fs.mkdirSync(path.join(dir, 'libdir'));
    write('libdir/helper.js', "export const greet = n => 'hello ' + n;\n");
  },

  'without -M a bare name is a builtin and stays an import'() {
    const r = run('-o', 'out.js', 'main.js');

    eq(r.status, 0);
    assert(/from 'mylib'/.test(read('out.js')), read('out.js'));
    assert(!/start of/.test(read('out.js')));
  },

  '-M NAME=PATH bundles the mapped file'() {
    const r = run('-M', 'mylib=./helper.js', '-o', 'out.js', 'main.js');
    const text = read('out.js');

    eq(r.status, 0);
    assert(/start of 'helper\.js'/.test(text), text);
    assert(/export const greet/.test(text), text);
    assert(!/from 'mylib'/.test(text), text);
  },

  'the builtin imports are hoisted once, not repeated in the bundled body'() {
    run('-M', 'mylib=./helper.js', '-o', 'out.js', 'main.js');

    const text = read('out.js');

    eq(text.match(/^import .* from 'util';$/gm).length, 1);
    eq(text.match(/^import .* from 'os';$/gm).length, 1);
  },

  'an absolute PATH works the same as a relative one'() {
    const r = run('-M', 'mylib=' + path.join(dir, 'helper.js'), '-o', 'out.js', 'main.js');

    eq(r.status, 0);
    assert(/start of 'helper\.js'/.test(read('out.js')), read('out.js'));
  },

  'NAME/sub/path with a mapped directory'() {
    const r = run('-M', 'pkg=./libdir', '-o', 'out.js', 'sub.js');

    eq(r.status, 0);
    assert(/hello/.test(read('out.js')), read('out.js'));
  },

  '-M may be repeated'() {
    write('both.js', "import { greet } from 'mylib';\nimport { greet as g2 } from 'pkg/helper.js';\nconsole.log(greet('a'), g2('b'));\n");

    const r = run('-M', 'mylib=./helper.js', '-M', 'pkg=./libdir', '-o', 'out.js', 'both.js');

    eq(r.status, 0);
    assert(/start of 'helper\.js'/.test(read('out.js')) && /start of 'libdir\/helper\.js'/.test(read('out.js')), read('out.js'));
  },

  'a malformed or missing mapping is an error'() {
    const a = run('-M', 'mylib', '-o', 'out.js', 'main.js');
    const b = run('-M', 'mylib=./nope.js', '-o', 'out.js', 'main.js');

    eq(a.status, 1);
    assert(/expects NAME=PATH/.test(a.stdout + a.stderr), a.stdout + a.stderr);
    eq(b.status, 1);
    assert(/does not exist/.test(b.stdout + b.stderr), b.stdout + b.stderr);
  },

  '-d prints the dependency tree'() {
    const r = run('-d', '-M', 'mylib=./helper.js', '-o', 'out.js', 'main.js');

    eq(r.status, 0);
    assert(/main\.js \(0\)\s+└─ helper\.js \(1\)/.test(r.stdout), r.stdout);
  },

  '-q silences diagnostics but keeps the output'() {
    const loud = run('-M', 'mylib=./helper.js', '-o', 'out.js', 'main.js');
    const quiet = run('-q', '-M', 'mylib=./helper.js', '-o', 'out.js', 'main.js');

    eq(quiet.status, 0);
    eq(quiet.stdout, '');
    assert(loud.stdout.length > 0, 'expected diagnostics without -q');
    assert(/start of 'helper\.js'/.test(read('out.js')), read('out.js'));
  },

  'an unresolved import exits 1 and reports its location'() {
    write('bad.js', "import { a } from './nope.js';\n");

    for(const args of [['-o', 'out.js', 'bad.js'], ['-q', '-o', 'out.js', 'bad.js']]) {
      const r = run(...args);

      eq(r.status, 1);
      assert(/bad\.js:1:1: cannot resolve module '\.\/nope\.js'/.test(r.stdout), r.stdout);
    }
  },

  'list-imports lists the imports of the given file, builtins included'() {
    write('li.js', "import { greet } from './helper.js';\nimport * as os from 'os';\nimport def, { a as b } from 'somepkg';\n");
    fs.symlinkSync(script, path.join(dir, 'list-imports.js'));

    const log = path.join(dir, 'li.log');
    const fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
    const status = os.exec([process.execPath, path.join(dir, 'list-imports.js'), 'li.js'], { cwd: dir, stdout: fd, stderr: fd, block: true });

    os.close(fd);

    const text = read('li.log');

    eq(status, 0);
    assert(/^li\.js:helper\.js greet$/m.test(text), text);
    assert(/^li\.js:os /m.test(text), text);
    assert(/^li\.js:somepkg .*b/m.test(text), text);
  },

  '-q still reports argument errors'() {
    const r = run('-q', '-M', 'bogus', 'main.js');

    assert(r.status != 0 && r.stdout.length > 0, r.stdout);
  },

  'a comment inside a bundled import statement is removed with -C'() {
    write('cm.js', "import { greet /* inline note */ } from 'mylib';\n// trailing comment\nconsole.log(greet('x'));\n");

    const r = run('-C', '-M', 'mylib=./helper.js', '-o', 'out.js', 'cm.js');
    const text = read('out.js');

    eq(r.status, 0);
    assert(/start of 'helper\.js'/.test(text) && !/inline note|trailing comment/.test(text), text);
    assert(/console\.log\(greet\('x'\)\);/.test(text), text);
  },

  'a side-effect import is bundled'() {
    write('side.js', "globalThis.sideEffect = 1;\n");
    write('se.js', "import './side.js';\nconsole.log(globalThis.sideEffect);\n");

    const r = run('-o', 'out.js', 'se.js');

    eq(r.status, 0);
    assert(/start of 'side\.js'/.test(read('out.js')), read('out.js'));
  },

  '-G assigns the exported names of an export list; -E drops the list'() {
    write('lib1.js', 'export const k = 7;\n');
    write('g.js', "import { k } from './lib1.js';\nexport { b as bee, c };\nconst b = 3, c = 4;\nconsole.log(k, b, c);\n");

    eq(run('-G', '-o', 'out.js', 'g.js').status, 0);
    assert(/Object\.assign\(globalThis, \{ bee: b, c \}\);/.test(read('out.js')), read('out.js'));
    assert(/^export \{ b as bee, c \};$/m.test(read('out.js')), read('out.js'));

    eq(run('-G', '-E', '-o', 'out.js', 'g.js').status, 0);
    assert(!/^export \{ b as bee, c \};$/m.test(read('out.js')), read('out.js'));
  },

  'a package directory resolves through exports, main or index.js'() {
    for(const [name, pkg, file] of [
      ['pkgmain', { main: 'entry.js' }, 'entry.js'],
      ['pkgexports', { exports: { '.': { import: './esm.js', require: './cjs.js' } } }, 'esm.js'],
      ['pkgstring', { exports: './str.js' }, 'str.js'],
      ['pkgindex', { name: 'x' }, 'index.js'],
    ]) {
      fs.mkdirSync(path.join(dir, name));
      write(name + '/package.json', JSON.stringify(pkg));
      write(name + '/' + file, `globalThis.${name} = 1;\n`);
      write(`use-${name}.js`, `import './${name}';\n`);

      eq(run('-o', 'out.js', `use-${name}.js`).status, 0, name);
      assert(new RegExp(`start of '${name}/${file.replace('.', '\\.')}'`).test(read('out.js')), name + ':\n' + read('out.js'));
    }
  },

  '--merge rewrites the file in place and keeps its mode'() {
    write('mg.js', "import { a } from 'os';\nimport { b } from 'os';\nconsole.log(a, b);\n");
    fs.chmodSync(path.join(dir, 'mg.js'), 0o640);
    write('mg.js.new', 'stale');

    const r = run('-m', 'mg.js');

    eq(r.status, 0);
    eq(read('mg.js'), "import { a, b } from 'os';\nconsole.log(a, b);\n");
    eq(fs.statSync(path.join(dir, 'mg.js')).mode & 0o777, 0o640);
    assert(!fs.existsSync(path.join(dir, 'mg.js.new')), 'temporary file left behind');
  },

  'cleanup'() {
    fs.rmSync(dir, { recursive: true, force: true });
  },
});
