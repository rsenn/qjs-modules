/* lib/typescript.js: .ts loader through the external swc CLI, qjsm only */
import installTypeScript from '../../lib/typescript.js';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-typescript-${os.getpid()}`;
const hasSwc = os.exec(['sh', '-c', 'command -v swc >/dev/null']) === 0;

function write(name, text) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.puts(text);
  f.close();
}

let hooks;

const suite = {
  'setup'() {
    os.mkdir(dir);
    write('a.ts', 'export class A<T> { constructor(public v: T) {} get(): T { return this.v; } }\nexport const n: number = 5;\nexport default function f(p: { x: number }): number { return p.x; }\n');
    write('uses.ts', "import f, { A, n } from './a.ts';\nimport type { Nope } from './nope';\nexport const r: string = [new A('s').get(), n, f({ x: 3 })].join();\n");
    write('m.mts', 'export const m: string = "mts";\n');
    write('c.tsx', 'export const c = <T,>(x: T): T => x;\n');
    os.mkdir(`${dir}/sub`);
    write('sub/index.ts', 'export const idx: number = 7;\n');
    write('bad.ts', 'let x: = 1\n');
    write('enum.ts', 'export enum Color { Red, Green }\nexport const g: Color = Color.Green;\n');

    hooks = installTypeScript();
  },

  async 'a .ts module loads with its types stripped'() {
    const m = await import(`${dir}/a.ts`);

    eq(5, m.n);
    eq('x', new m.A('x').get());
    eq(9, m.default({ x: 9 }));
  },

  async 'imports between .ts files work'() {
    eq('s,5,3', (await import(`${dir}/uses.ts`)).r);
  },

  async '.mts is loaded'() {
    eq('mts', (await import(`${dir}/m.mts`)).m);
  },

  async '.tsx is loaded'() {
    eq(3, (await import(`${dir}/c.tsx`)).c(3));
  },

  async 'an extensionless specifier finds the .ts file'() {
    eq(5, (await import(`${dir}/a`)).n);
  },

  async 'a directory specifier finds index.ts'() {
    eq(7, (await import(`${dir}/sub`)).idx);
  },

  async 'enums are compiled'() {
    eq(1, (await import(`${dir}/enum.ts`)).g);
  },

  async 'a syntax error rejects the import'() {
    let error;

    try {
      await import(`${dir}/bad.ts`);
    } catch(e) {
      error = e;
    }

    assert(error && /swc failed/.test(error.message), String(error));
  },

  'teardown'() {
    hooks.deregister();
    os.exec(['rm', '-rf', dir]);
  },
};

if(hasSwc) tests(suite);
else console.log('SKIP: swc not found in PATH');
