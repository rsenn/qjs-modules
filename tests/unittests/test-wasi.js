// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
import { closeSync, constants, existsSync, mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from 'fs';
import { WASI, errno } from '../../lib/wasi.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* qjsm has no global WebAssembly until lib/webassembly.js is imported */
if(!globalThis.WebAssembly) await import('../../lib/webassembly.js');

const wasm = WebAssembly;
const HEAD = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

/* (memory (export "mem") 1): a guest memory for calling the imports by hand */
const MEMORY = new Uint8Array([...HEAD, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x07, 0x01, 0x03, 0x6d, 0x65, 0x6d, 0x02, 0x00]);

const fixture = name => new wasm.Module(readFileSync(`tests/fixtures/wasi/${name}.wasm`));

/* a scratch directory with a WASI whose stdout is a file in it */
function setup(options = {}) {
  const dir = mkdtempSync('/tmp/wasi-test-');
  const out = `${dir}/stdout`;
  const fd = openSync(out, constants.O_WRONLY | constants.O_CREAT | constants.O_TRUNC, 0o644);
  const wasi = new WASI({ preopens: { '/sandbox': dir }, stdout: fd, ...options });

  return {
    dir,
    wasi,
    stdout: () => readFileSync(out, 'utf8'),
    done() {
      closeSync(fd);
      rmSync(dir, { recursive: true, force: true });
    },
  };
}

/* a WASI bound to a bare memory, plus helpers to put and read guest strings */
function bound(options) {
  const env = setup(options);
  const mem = new wasm.Instance(new wasm.Module(MEMORY)).exports.mem;

  env.wasi.finalizeBindings(null, { memory: mem });
  env.u8 = () => new Uint8Array(mem.buffer);
  env.view = () => new DataView(mem.buffer);
  env.put = (ptr, s) => {
    const b = [...unescape(encodeURIComponent(s))].map(c => c.charCodeAt(0));

    env.u8().set(b, ptr);
    return b.length;
  };
  env.imp = env.wasi.wasiImport;
  return env;
}

tests({
  'getImportObject() names the preview1 module'() {
    const { wasi, done } = setup();

    assert('wasi_snapshot_preview1' in wasi.getImportObject());
    assert('wasi_unstable' in new WASI({ version: 'unstable' }).getImportObject());
    done();
  },
  'rejects a bad version and bad args'() {
    let e1, e2;

    try {
      new WASI({ version: 'x' });
    } catch(e) {
      e1 = e;
    }
    try {
      new WASI({ args: 'x' });
    } catch(e) {
      e2 = e;
    }
    assert(e1 instanceof TypeError);
    assert(e2 instanceof TypeError);
  },
  'start() returns the proc_exit code'() {
    const { wasi, done } = setup();
    const inst = new wasm.Instance(fixture('exit3'), wasi.getImportObject());

    eq(wasi.start(inst), 3);
    done();
  },
  'start() runs a guest that writes to stdout'() {
    const env = setup();
    const inst = new wasm.Instance(fixture('hello'), env.wasi.getImportObject());

    eq(env.wasi.start(inst), 0);
    eq(env.stdout(), 'hello\n');
    env.done();
  },
  'a second start() throws ERR_WASI_ALREADY_STARTED'() {
    const env = setup();
    const inst = new wasm.Instance(fixture('hello'), env.wasi.getImportObject());
    let err;

    env.wasi.start(inst);
    try {
      env.wasi.start(inst);
    } catch(e) {
      err = e;
    }
    eq(err?.code, 'ERR_WASI_ALREADY_STARTED');
    env.done();
  },
  'the guest sees args'() {
    const env = setup({ args: ['prog', 'x'] });

    env.wasi.start(new wasm.Instance(fixture('args'), env.wasi.getImportObject()));
    eq(env.stdout(), 'prog\0x\0');
    env.done();
  },
  'the guest reads a file through a preopen'() {
    const env = setup();

    writeFileSync(`${env.dir}/in.txt`, 'from the host');
    env.wasi.start(new wasm.Instance(fixture('cat'), env.wasi.getImportObject()));
    eq(env.stdout(), 'from the host');
    env.done();
  },
  'args and environ are written NUL-terminated'() {
    const env = bound({ args: ['a', 'bc'], env: { K: 'v' } });
    const { imp, view, u8 } = env;

    eq(imp.args_sizes_get(0, 4), 0);
    eq([view().getUint32(0, true), view().getUint32(4, true)].join(), '2,5');
    eq(imp.args_get(16, 64), 0);
    eq([...u8().subarray(64, 69)].join(), '97,0,98,99,0');
    eq(view().getUint32(20, true), 66);
    eq(imp.environ_sizes_get(0, 4), 0);
    eq([view().getUint32(0, true), view().getUint32(4, true)].join(), '1,4');
    eq(imp.environ_get(16, 64), 0);
    eq(String.fromCharCode(...u8().subarray(64, 67)), 'K=v');
    env.done();
  },
  'clock_time_get and random_get'() {
    const env = bound();
    const { imp, view, u8 } = env;

    eq(imp.clock_time_get(0, 0n, 0), 0);
    assert(view().getBigUint64(0, true) > 1600000000000000000n);
    eq(imp.clock_time_get(1, 0n, 0), 0);
    eq(imp.clock_time_get(9, 0n, 0), errno.INVAL);
    eq(imp.random_get(64, 16), 0);
    assert(u8().subarray(64, 80).some(b => b != 0));
    env.done();
  },
  'prestat describes the preopen'() {
    const env = bound();
    const { imp, view, u8 } = env;

    eq(imp.fd_prestat_get(3, 0), 0);
    eq(view().getUint32(4, true), '/sandbox'.length);
    eq(imp.fd_prestat_dir_name(3, 64, 8), 0);
    eq(String.fromCharCode(...u8().subarray(64, 72)), '/sandbox');
    eq(imp.fd_prestat_get(4, 0), errno.BADF);
    env.done();
  },
  'path_open creates, writes, seeks and reads a file'() {
    const env = bound();
    const { imp, view, put, dir } = env;
    const n = put(100, 'new.txt');

    eq(imp.path_open(3, 1, 100, n, 1, 66n, 0n, 0, 0), 0); /* CREAT; FD_READ|FD_WRITE */

    const fd = view().getUint32(0, true);

    put(200, 'hello world');
    view().setUint32(8, 200, true);
    view().setUint32(12, 11, true);
    eq(imp.fd_write(fd, 8, 1, 4), 0);
    eq(view().getUint32(4, true), 11);
    eq(imp.fd_seek(fd, 6n, 0, 16), 0);
    eq(view().getBigUint64(16, true), 6n);
    view().setUint32(8, 300, true);
    view().setUint32(12, 64, true);
    eq(imp.fd_read(fd, 8, 1, 4), 0);
    eq(view().getUint32(4, true), 5);
    eq(imp.fd_filestat_get(fd, 400), 0);
    eq(view().getBigUint64(432, true), 11n);
    eq(imp.fd_close(fd), 0);
    eq(readFileSync(`${dir}/new.txt`, 'utf8'), 'hello world');
    env.done();
  },
  'a path that escapes the preopen is NOTCAPABLE'() {
    const env = bound();
    const { imp, put } = env;

    for(const p of ['../x', 'a/../../x', '/etc/passwd']) {
      const n = put(100, p);

      eq(imp.path_open(3, 1, 100, n, 0, 2n, 0n, 0, 0), errno.NOTCAPABLE);
    }
    env.done();
  },
  'a missing file is NOENT, and EXCL on an existing one is EXIST'() {
    const env = bound();
    const { imp, put, dir } = env;
    let n = put(100, 'nope');

    eq(imp.path_open(3, 1, 100, n, 0, 2n, 0n, 0, 0), errno.NOENT);
    writeFileSync(`${dir}/there`, 'x');
    n = put(100, 'there');
    eq(imp.path_open(3, 1, 100, n, 1 | 4, 2n, 0n, 0, 0), errno.EXIST);
    env.done();
  },
  'directories: create, list, remove'() {
    const env = bound();
    const { imp, view, put, u8, dir } = env;
    let n = put(100, 'sub');

    eq(imp.path_create_directory(3, 100, n), 0);
    assert(existsSync(`${dir}/sub`));
    writeFileSync(`${dir}/sub/a`, '');
    writeFileSync(`${dir}/sub/b`, '');
    eq(imp.path_open(3, 1, 100, n, 2, 2n, 0n, 0, 0), 0);

    const fd = view().getUint32(0, true);

    eq(imp.fd_readdir(fd, 512, 1024, 0n, 4), 0);

    /* entries: d_next u64, d_ino u64, d_namlen u32, d_type u8, name */
    const used = view().getUint32(4, true), names = [];

    for(let at = 512; at < 512 + used; ) {
      const len = view().getUint32(at + 16, true);

      names.push(String.fromCharCode(...u8().subarray(at + 24, at + 24 + len)));
      at += 24 + len;
    }
    eq(names.filter(x => x != '.' && x != '..').sort().join(), 'a,b');
    eq(imp.fd_close(fd), 0);
    eq(imp.path_remove_directory(3, 100, n), errno.NOTEMPTY);
    n = put(100, 'sub/a');
    eq(imp.path_unlink_file(3, 100, n), 0);
    n = put(100, 'sub/b');
    eq(imp.path_unlink_file(3, 100, n), 0);
    n = put(100, 'sub');
    eq(imp.path_remove_directory(3, 100, n), 0);
    assert(!existsSync(`${dir}/sub`));
    env.done();
  },
  'path_rename and path_filestat_get'() {
    const env = bound();
    const { imp, view, put, dir } = env;

    writeFileSync(`${dir}/old`, 'abc');

    const on = put(100, 'old'), nn = put(110, 'new');

    eq(imp.path_rename(3, 100, on, 3, 110, nn), 0);
    eq(imp.path_filestat_get(3, 1, 110, nn, 400), 0);
    eq(view().getBigUint64(432, true), 3n);
    eq(view().getUint8(416), 4); /* REGULAR_FILE */
    eq(imp.path_filestat_get(3, 1, 100, on, 400), errno.NOENT);
    env.done();
  },
  'a closed or unknown fd is BADF'() {
    const env = bound();

    eq(env.imp.fd_close(99), errno.BADF);
    eq(env.imp.fd_seek(99, 0n, 0, 0), errno.BADF);
    eq(env.imp.fd_write(99, 0, 0, 0), errno.BADF);
    env.done();
  },
  'sockets are not implemented'() {
    const env = bound();

    eq(env.imp.sock_accept(0, 0, 0), errno.NOSYS);
    env.done();
  },
});
