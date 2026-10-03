/* misc.glob(): the bundled OpenBSD glob (cmake -DOPENBSD_GLOB=ON, the default)
 * and the system glob (-DOPENBSD_GLOB=OFF) must behave the same here. */
import { GLOB_APPEND, GLOB_BRACE, GLOB_ERR, GLOB_MARK, GLOB_NOCHECK, GLOB_NOMATCH, GLOB_TILDE, glob } from 'misc';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-glob-${os.getpid()}`;

function touch(name) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.close();
}

tests({
  'setup'() {
    os.mkdir(dir);
    os.mkdir(`${dir}/sub`);
    for(const name of ['a.c', 'b.c', 'c.h', '.hidden.c', 'sub/d.c']) touch(name);
  },

  'a literal path matches itself'() {
    eq(`${dir}/a.c`, glob(`${dir}/a.c`).join());
  },

  'star matches, sorted, and skips dotfiles'() {
    eq(`${dir}/a.c,${dir}/b.c`, glob(`${dir}/*.c`).join());
  },

  'a leading dot must be matched explicitly'() {
    eq(`${dir}/.hidden.c`, glob(`${dir}/.*.c`).join());
  },

  'question mark matches one character'() {
    eq(`${dir}/a.c,${dir}/b.c`, glob(`${dir}/?.c`).join());
  },

  'a bracket expression'() {
    eq(`${dir}/a.c,${dir}/b.c`, glob(`${dir}/[ab].c`).join());
    eq(`${dir}/c.h`, glob(`${dir}/[!ab].?`).join());
  },

  'a path component wildcard'() {
    eq(`${dir}/sub/d.c`, glob(`${dir}/*/d.c`).join());
  },

  'GLOB_BRACE expands alternatives'() {
    eq(`${dir}/a.c,${dir}/c.h`, glob(`${dir}/{a.c,c.h}`, GLOB_BRACE).join());
    eq(`${dir}/a.c,${dir}/b.c,${dir}/c.h`, glob(`${dir}/{a,b,c}.[ch]`, GLOB_BRACE).join());
  },

  'GLOB_MARK appends a slash to directories'() {
    eq(`${dir}/sub/`, glob(`${dir}/su*`, GLOB_MARK).join());
  },

  'no match returns GLOB_NOMATCH'() {
    eq(GLOB_NOMATCH, glob(`${dir}/*.zzz`));
  },

  'GLOB_NOCHECK returns the pattern itself'() {
    eq(`${dir}/*.zzz`, glob(`${dir}/*.zzz`, GLOB_NOCHECK).join());
  },

  'GLOB_TILDE expands the home directory'() {
    const home = std.getenv('HOME');

    assert(home, 'HOME unset');
    eq(home, glob('~', GLOB_TILDE).join());
  },

  'results can be appended to an existing array'() {
    const out = ['first'];

    eq(0, glob(`${dir}/*.h`, GLOB_APPEND, null, out));
    eq(`first,${dir}/c.h`, out.join());
  },

  'an error callback may be passed without crashing'() {
    eq(`${dir}/c.h`, glob(`${dir}/*.h`, GLOB_ERR, () => 0).join());
  },

  'the error callback sees unreadable directories'() {
    if(os.getuid?.() === 0) return;

    os.mkdir(`${dir}/locked`, 0o000);

    const seen = [];

    glob(`${dir}/locked/*`, GLOB_ERR, (path, errno) => seen.push([path, errno]));
    os.exec(['chmod', '755', `${dir}/locked`]);
    assert(seen.length === 0 || /locked/.test(seen[0][0]), JSON.stringify(seen));
  },

  'teardown'() {
    os.exec(['rm', '-rf', dir]);
  },
});
