/* lib/loaders/packages.js and import.meta.resolve(): Node's package
 * resolution on qjsm. The expected values are what Node 23.11's
 * import.meta.resolve() returns for the same fixture, Bun's resolveSync()
 * agrees on every package case. */
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as std from 'std';
import { legacyMainResolve, packageExportsResolve, packageImportsResolve, packageResolve, readPackageJson } from 'loaders/packages';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = fs.mkdtempSync(path.join(std.getenv('TMPDIR') || '/tmp', 'loaders-packages-'));
const app = path.join(dir, 'app');
const parent = path.join(app, 'src/main.js');
const nm = 'node_modules/';

function write(name, text) {
  fs.mkdirSync(path.dirname(path.join(app, name)), { recursive: true });
  fs.writeFileSync(path.join(app, name), typeof text == 'string' ? text : JSON.stringify(text));
}

/* specifier as resolved from app/src/main.js, app/ cut off, ERR <code> on a throw */
function resolve(specifier, from = parent) {
  try {
    return import.meta.resolve(specifier, from).replace('file://' + app + '/', '');
  } catch(e) {
    return 'ERR ' + e.code;
  }
}

/* runs qjsm on a script in app/src, returns its output */
function run(args, script) {
  const log = path.join(dir, 'run.log');

  write('src/run.js', script);

  const fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);

  os.exec([process.execPath, ...args, 'run.js'], { cwd: path.join(app, 'src'), stdout: fd, stderr: fd, block: true });
  os.close(fd);

  return fs.readFileSync(log, 'utf-8').trim();
}

tests({
  'setup'() {
    write('package.json', { name: 'app', type: 'module', imports: { '#int': './src/int.js', '#dep': 'dep', '#pat/*': './src/pat/*.js' }, exports: { '.': './src/main.js', './feat/*': './src/feat/*.js' } });

    for(const f of ['main.js', 'int.js', 'pat/a.js', 'feat/x.js', 'plain.js']) write('src/' + f, 'export default 1;\n');

    write(nm + 'cond/package.json', { name: 'cond', exports: { bun: './bun.js', deno: './deno.js', node: { import: './node-import.js', require: './node-require.js' }, import: './import.js', require: './require.js', default: './default.js' } });
    write(nm + 'cond2/package.json', { name: 'cond2', exports: { module: './module.js', import: './import.js', default: './default.js' } });
    write(nm + 'cond3/package.json', { name: 'cond3', exports: { browser: './browser.js', production: './prod.js', default: './default.js' } });
    write(nm + 'legacy/package.json', { name: 'legacy', main: 'lib/main', module: 'esm/index.js' });
    write(nm + 'legacy2/package.json', { name: 'legacy2', main: 'lib' });
    write(nm + 'nomain/package.json', { name: 'nomain' });
    write(nm + '@sc/pkg/package.json', { name: '@sc/pkg', exports: { '.': './i.js', './sub': './s.js', './pat/*.js': './p/*.mjs', './hidden/*': null } });
    write(nm + 'arr/package.json', { name: 'arr', exports: { '.': ['./missing-but-valid.js', './second.js'] } });
    write(nm + 'sugar/package.json', { name: 'sugar', exports: './s.js' });
    write(nm + 'noexp/package.json', { name: 'noexp', main: 'm.js' });
    write(nm + 'dep/package.json', { name: 'dep', exports: './d.js' });

    for(const d of ['cond', 'cond2', 'cond3']) for(const f of ['bun', 'deno', 'node-import', 'node-require', 'import', 'module', 'require', 'default', 'browser', 'prod']) write(nm + d + '/' + f + '.js', `export default '${d}/${f}';\n`);
    for(const f of ['legacy/lib/main.js', 'legacy/lib/index.js', 'legacy/esm/index.js', 'legacy2/lib/index.js', 'nomain/index.js', '@sc/pkg/i.js', '@sc/pkg/s.js', '@sc/pkg/p/a.mjs', '@sc/pkg/hidden/x.js', 'arr/second.js', 'sugar/s.js', 'noexp/m.js', 'noexp/other.js', 'dep/d.js']) write(nm + f, '1;\n');
  },

  'relative and absolute specifiers are joined, not checked'() {
    eq('src/plain.js', resolve('./plain.js'));
    eq('src/noext', resolve('./noext'));
    eq('src/plain.js', resolve('../src/plain.js'));
    eq('file:///etc/hostname', import.meta.resolve('/etc/hostname', parent));
  },

  '"imports": names, a package target, a pattern'() {
    eq('src/int.js', resolve('#int'));
    eq(nm + 'dep/d.js', resolve('#dep'));
    eq('src/pat/a.js', resolve('#pat/a'));
    eq('ERR ERR_PACKAGE_IMPORT_NOT_DEFINED', resolve('#missing'));
  },

  'a package refers to itself by its name'() {
    eq('src/main.js', resolve('app'));
    eq('src/feat/x.js', resolve('app/feat/x'));
  },

  'conditions: the first key that is node, import or default'() {
    eq(nm + 'cond/node-import.js', resolve('cond'));
    eq(nm + 'cond2/import.js', resolve('cond2'));
    eq(nm + 'cond3/default.js', resolve('cond3'));
  },

  'no "exports": main with .js or /index.js, then index.js'() {
    eq(nm + 'legacy/lib/main.js', resolve('legacy'));
    eq(nm + 'legacy2/lib/index.js', resolve('legacy2'));
    eq(nm + 'nomain/index.js', resolve('nomain'));
    eq(nm + 'noexp/m.js', resolve('noexp'));
    eq(nm + 'noexp/other.js', resolve('noexp/other.js'));
  },

  '"exports": scoped names, subpaths, patterns, null, sugar, arrays'() {
    eq(nm + '@sc/pkg/i.js', resolve('@sc/pkg'));
    eq(nm + '@sc/pkg/s.js', resolve('@sc/pkg/sub'));
    eq(nm + '@sc/pkg/p/a.mjs', resolve('@sc/pkg/pat/a.js'));
    eq('ERR ERR_PACKAGE_PATH_NOT_EXPORTED', resolve('@sc/pkg/hidden/x.js'));
    eq('ERR ERR_PACKAGE_PATH_NOT_EXPORTED', resolve('@sc/pkg/nope'));
    eq(nm + 'sugar/s.js', resolve('sugar'));
    eq(nm + 'arr/missing-but-valid.js', resolve('arr'));
    eq('ERR ERR_INVALID_MODULE_SPECIFIER', resolve('@sc'));
  },

  'builtins: node:<name> for a Node module, the name for a qjsm one'() {
    eq('node:fs', import.meta.resolve('fs'));
    eq('node:fs', import.meta.resolve('node:fs'));
    eq('node:fs/promises', import.meta.resolve('fs/promises'));
    eq('std', import.meta.resolve('std'));
    eq('os', import.meta.resolve('os'));
    eq('ERR ERR_MODULE_NOT_FOUND', resolve('nope-x'));
  },

  'one argument resolves from the module itself'() {
    eq(import.meta.url.replace(/[^/]*$/, 'x.js'), import.meta.resolve('./x.js'));
  },

  'the functions behind the hook take and give paths'() {
    eq(path.join(app, nm + 'cond/node-import.js'), packageResolve('cond', parent));
    eq(path.join(app, nm + 'cond/import.js'), packageResolve('cond', parent, ['import']));
    eq(undefined, packageResolve('nope-x', parent));
    eq(path.join(app, 'src/int.js'), packageImportsResolve('#int', parent));
    eq(path.join(app, nm + '@sc/pkg/s.js'), packageExportsResolve(path.join(app, nm + '@sc/pkg'), './sub', readPackageJson(path.join(app, nm + '@sc/pkg')).exports));
    eq(path.join(app, nm + 'legacy/lib/main.js'), legacyMainResolve(path.join(app, nm + 'legacy'), readPackageJson(path.join(app, nm + 'legacy'))));
  },

  'an import loads the resolved file, -C adds a condition, -N turns it off'() {
    const script = "import a from 'cond';\nimport b from 'cond3';\nconsole.log(a, b);\n";

    eq('cond/node-import cond3/default', run([], script));
    eq('cond/node-import cond3/prod', run(['-C', 'production'], script));
    eq('cond/node-import cond3/browser', run(['--conditions=browser'], script));
    assert(/could not load module 'cond'/.test(run(['-N'], script)), run(['-N'], script));
  },

  'cleanup'() {
    fs.rmSync(dir, { recursive: true, force: true });
  },
});
