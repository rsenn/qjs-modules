/* test_commonjs_typescript.js: lib/commonjs.js and lib/typescript.js.
 * qjsm only; the TypeScript part is skipped when `swc` is not in PATH. */
import installCommonJS from '../lib/commonjs.js';
import installTypeScript from '../lib/typescript.js';
import * as std from 'std';
import * as os from 'os';
import { createRequire } from 'module';

let failed = 0;

function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);

  if(!ok) failed++;

  console.log(`${ok ? 'PASS' : 'FAIL'}: ${name}${ok ? '' : ` (got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)})`}`);
}

const dir = `/tmp/qjsm-test-cjs-ts-${os.getpid()}`;

function write(name, text) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.puts(text);
  f.close();
}

os.mkdir(dir);
write('dep.cjs', 'module.exports = { v: 41 };\n');
write('num.cjs', 'module.exports = 41;\n');
write('lib.cjs', "const dep = require('./dep.cjs');\nexports.sum = dep.v + 1;\nexports.file = __filename;\n");
write('t.ts', 'export class A<T> { constructor(public v: T) {} }\nexport const n: number = 5;\nexport default function f(p: { x: number }): number { return p.x; }\n');
write('uses.ts', "import f, { A, n } from './t.ts';\nexport const r: string = [new A('s').v, n, f({ x: 3 })].join();\n");

installCommonJS();
check('require is global', typeof globalThis.require, 'function');

const cjs = await import(`${dir}/lib.cjs`);
check('cjs named export', cjs.sum, 42);
check('cjs __filename', cjs.file, `${dir}/lib.cjs`);
check('cjs default is module.exports', cjs.default.sum, 42);

const req = createRequire(`file://${dir}/x.js`);
check('createRequire primitive export', req('./num.cjs'), 41);
check('createRequire nested require', req('./lib.cjs').sum, 42);

const [, swc] = [0, os.exec(['sh', '-c', 'command -v swc >/dev/null'])];

if(swc === 0) {
  installTypeScript();
  check('ts import', (await import(`${dir}/uses.ts`)).r, 's,5,3');
  check('ts extensionless', (await import(`${dir}/t`)).n, 5);
} else {
  console.log('SKIP: swc not found');
}

os.exec(['rm', '-rf', dir]);
console.log(failed ? `${failed} FAILED` : 'SUCCESS');
std.exit(failed ? 1 : 0);
