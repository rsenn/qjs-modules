// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/node/os.js: Node's os on node:os */
import os, { constants, cpus, EOL, userInfo } from 'node:os';
import { assert, eq, tests } from '../../lib/tinytest.js';

const code = fn => {
  try {
    fn();
  } catch(e) {
    return e.code;
  }
};

/* key order differs between runtimes */
const keys = obj => Object.keys(obj).sort().join();

tests({
  'default export and named exports are the same functions'() {
    eq(cpus, os.cpus);
    eq(EOL, '\n');
    eq(os.devNull, '/dev/null');
  },
  'uname facts are strings'() {
    for(const name of ['arch', 'platform', 'type', 'release', 'version', 'machine', 'hostname']) assert(typeof os[name]() == 'string' && os[name]() != '', name);
    eq(os.platform(), 'linux');
    eq(os.type(), 'Linux');
    eq(`${os.hostname}`, os.hostname());
  },
  'endianness is LE or BE'() {
    assert(['LE', 'BE'].includes(os.endianness()));
  },
  'tmpdir follows TMPDIR without the trailing slash'() {
    const saved = process.env.TMPDIR;

    process.env.TMPDIR = '/var/tmp/';
    eq(os.tmpdir(), '/var/tmp');
    if(saved === undefined) delete process.env.TMPDIR;
    else process.env.TMPDIR = saved;
  },
  'userInfo has the effective user'() {
    const u = userInfo();

    eq(keys(u), 'gid,homedir,shell,uid,username');
    eq(typeof u.uid, 'number');
    assert(u.username.length > 0);
    eq(userInfo({ encoding: 'buffer' }).username.toString(), u.username);
  },
  'cpus has one entry per CPU with times'() {
    const list = cpus();

    assert(list.length > 0);
    eq(typeof list[0].model + typeof list[0].speed, 'stringnumber');
    eq(keys(list[0].times), 'idle,irq,nice,sys,user');
    assert(os.availableParallelism() >= 1);
  },
  'memory, load and uptime are numbers'() {
    assert(os.totalmem() > os.freemem() && os.freemem() > 0);
    eq(os.loadavg().length, 3);
    assert(os.uptime() > 0);
  },
  'getPriority validates and reads the nice value'() {
    eq(typeof os.getPriority(), 'number');
    eq(code(() => os.getPriority('x')), 'ERR_INVALID_ARG_TYPE');
    eq(code(() => os.getPriority(1.5)), 'ERR_OUT_OF_RANGE');
    eq(code(() => os.getPriority(2 ** 30)), 'ERR_SYSTEM_ERROR');
    eq(code(() => os.setPriority(0, 100)), 'ERR_OUT_OF_RANGE');
  },
  'constants'() {
    eq(constants.signals.SIGINT, 2);
    eq(constants.signals.SIGIOT, constants.signals.SIGABRT);
    eq(constants.errno.ENOENT, 2);
    eq(constants.priority.PRIORITY_HIGHEST, -20);
    eq(constants.dlopen.RTLD_NOW, 2);
  },
});
