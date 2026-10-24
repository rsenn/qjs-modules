/* node/os.js: Node's os (https://nodejs.org/api/os.html) for node:os.
 * the bare os stays QuickJS's own module, require('os') gets this one.
 * depends on: misc (uname, ids, errno and signal numbers), std (open,
 * getenv), process (arch, platform), node/buffer.
 * rule: Linux facts come from /proc, /sys and /etc/passwd, read per call.
 *
 *   import os from 'node:os';
 *   os.cpus().length, os.totalmem(), os.userInfo().username
 *
 * limits:
 *   networkInterfaces()   throws InternalError, no getifaddrs(3) binding
 *   setPriority()         throws InternalError, no setpriority(2) binding
 *   userInfo()            reads /etc/passwd, not NSS (LDAP, sssd users)
 *   cpus() freemem() totalmem() loadavg() uptime() getPriority()
 *                         read /proc, InternalError where it is missing
 */
import * as misc from 'misc';
import { getenv, open } from 'std';
import { Buffer } from 'node/buffer';
import process from 'process';

const windows = process.platform == 'win32';

/* ---- errors ---- */

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });

const received = v => (typeof v == 'string' ? `type string ('${v}')` : v == null ? String(v) : `type ${typeof v} (${String(v)})`);

/* Node's SystemError for a failed libuv call, e.g. code ESRCH:
 *   A system error occurred: uv_os_getpriority returned ESRCH (no such process)
 */
function systemError(syscall, code, message) {
  const errno = -misc[code];
  const e = nodeError(Error, 'ERR_SYSTEM_ERROR', `A system error occurred: ${syscall} returned ${code} (${message})`);

  Object.defineProperty(e, 'name', { value: 'SystemError', writable: true, configurable: true });

  return Object.assign(e, { info: { errno, code, message, syscall }, errno, syscall });
}

/* Node's validateInt32: TypeError for a non-number, RangeError otherwise */
function int32(value, name, min = -(2 ** 31), max = 2 ** 31 - 1) {
  if(typeof value != 'number') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be of type number. Received ${received(value)}`);
  if(!Number.isInteger(value)) throw nodeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "${name}" is out of range. It must be an integer. Received ${value}`);
  if(value < min || value > max) throw nodeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "${name}" is out of range. It must be >= ${min} && <= ${max}. Received ${value}`);

  return value;
}

/* ---- files ---- */

/* whole text of a file, undefined when it cannot be opened.
 * std.loadFile() returns '' for /proc files, their size is 0 */
function readText(path) {
  const f = open(path, 'r');

  if(!f) return undefined;

  try {
    return f.readAsString();
  } finally {
    f.close();
  }
}

/* the same for a file a function cannot do without */
function procText(path) {
  const text = readText(path);

  /* XXX: TODO */
  if(text === undefined) throw new InternalError(`os: cannot read ${path} on ${process.platform}`);

  return text;
}

/* kB value of one /proc/meminfo line, in bytes: meminfo('MemTotal') */
const meminfo = key => +procText('/proc/meminfo').match(new RegExp(`^${key}:\\s+(\\d+)`, 'm'))[1] * 1024;

/* the /etc/passwd entry of uid as its 7 fields, undefined if it has none */
function passwd(uid) {
  for(const line of (readText('/etc/passwd') ?? '').split('\n')) {
    const fields = line.split(':');

    if(fields.length >= 7 && +fields[2] === uid) return fields;
  }
}

/* ---- constants ---- */

const table = entries => Object.assign(Object.create(null), Object.fromEntries(entries));

const miscNames = re => Object.keys(misc).filter(k => re.test(k) && typeof misc[k] == 'number' && misc[k] >= 0);

/* signals by number as in Node, SIGIOT is SIGABRT's other name */
const signals = table([...miscNames(/^SIG[A-Z0-9]+$/), 'SIGIOT'].map(name => [name, misc[name] ?? misc.SIGABRT]).sort((a, b) => a[1] - b[1]));

export const constants = table([
  ['UV_UDP_REUSEADDR', 4],
  [
    'dlopen',
    table([
      ['RTLD_LAZY', 1],
      ['RTLD_NOW', 2],
      ['RTLD_GLOBAL', 256],
      ['RTLD_LOCAL', 0],
      ['RTLD_DEEPBIND', 8],
    ]),
  ],
  [
    'errno',
    table(
      miscNames(/^E[A-Z0-9]+$/)
        .sort()
        .map(name => [name, misc[name]]),
    ),
  ],
  ['signals', signals],
  [
    'priority',
    table([
      ['PRIORITY_LOW', 19],
      ['PRIORITY_BELOW_NORMAL', 10],
      ['PRIORITY_NORMAL', 0],
      ['PRIORITY_ABOVE_NORMAL', -7],
      ['PRIORITY_HIGH', -14],
      ['PRIORITY_HIGHEST', -20],
    ]),
  ],
]);

export const EOL = windows ? '\r\n' : '\n';

export const devNull = windows ? '\\\\.\\nul' : '/dev/null';

/* ---- uname ---- */

export const arch = () => process.arch;

export const platform = () => process.platform;

export const type = () => misc.uname().sysname;

export const release = () => misc.uname().release;

export const version = () => misc.uname().version;

export const machine = () => misc.uname().machine;

export const hostname = () => misc.uname().nodename;

export const endianness = () => (new Uint8Array(new Uint16Array([1]).buffer)[0] ? 'LE' : 'BE');

/* ---- environment and user ---- */

/* $HOME, else the home directory of the effective user */
export function homedir() {
  return getenv('HOME') || passwd(misc.geteuid())?.[5];
}

export function tmpdir() {
  const path = getenv('TMPDIR') || getenv('TMP') || getenv('TEMP') || '/tmp';

  return path.length > 1 && path.endsWith('/') ? path.slice(0, -1) : path;
}

/* the effective user from /etc/passwd, strings or with encoding 'buffer'
 * Buffers: { uid: 1000, gid: 1000, username: 'roman', homedir, shell } */
export function userInfo(options) {
  const uid = misc.geteuid(),
    entry = passwd(uid);

  if(!entry) throw systemError('uv_os_get_passwd', 'ENOENT', 'no such file or directory');

  const encoding = options?.encoding;
  const str = s => (encoding == 'buffer' ? Buffer.from(s) : encoding && Buffer.isEncoding(encoding) ? Buffer.from(s).toString(encoding) : s);
  const [username, , , gid, , dir, shell] = entry;

  return table([
    ['uid', uid],
    ['gid', +gid],
    ['username', str(username)],
    ['homedir', str(dir)],
    ['shell', str(shell)],
  ]);
}

/* ---- processors, memory, load ---- */

/* one entry per /proc/stat cpuN line, times in ms (USER_HZ is 100):
 * { model: 'Intel(R) Core(TM) i7-6700 CPU', speed: 3300,
 *   times: { user, nice, sys, idle, irq } } */
export function cpus() {
  const info = procText('/proc/cpuinfo');
  const models = [...info.matchAll(/^model name\s*:\s*(.*)$/gm)].map(m => m[1]);
  const mhz = [...info.matchAll(/^cpu MHz\s*:\s*([\d.]+)/gm)].map(m => Math.floor(+m[1]));

  return [...procText('/proc/stat').matchAll(/^cpu(\d+) (.*)$/gm)].map(([, n, fields]) => {
    const [user, nice, sys, idle, , irq] = fields.split(' ').map(ticks => ticks * 10);
    const khz = readText(`/sys/devices/system/cpu/cpu${n}/cpufreq/scaling_cur_freq`);

    return {
      model: models[n] ?? models[0] ?? 'unknown',
      speed: khz ? Math.floor(khz / 1000) : (mhz[n] ?? 0),
      times: { user, nice, sys, idle, irq },
    };
  });
}

/* CPUs in this process's affinity mask, Cpus_allowed_list: 0-3,6 is 5 */
export function availableParallelism() {
  const list = readText('/proc/self/status')?.match(/^Cpus_allowed_list:\s*(\S+)/m)?.[1];
  const width = range => {
    const [first, last = first] = range.split('-');

    return last - first + 1;
  };

  return Math.max(1, list ? list.split(',').reduce((n, range) => n + width(range), 0) : cpus().length);
}

export const freemem = () => meminfo('MemAvailable');

export const totalmem = () => meminfo('MemTotal');

export const loadavg = () => procText('/proc/loadavg').split(' ', 3).map(Number);

export const uptime = () => parseFloat(procText('/proc/uptime'));

/* ---- priority ---- */

/* nice value of pid, field 19 of /proc/<pid>/stat, pid 0 is this process */
export function getPriority(pid = 0) {
  int32(pid, 'pid');

  const stat = readText(`/proc/${pid || 'self'}/stat`);

  if(stat === undefined) {
    procText('/proc/self/stat'); /* no /proc at all: InternalError */
    throw systemError('uv_os_getpriority', 'ESRCH', 'no such process');
  }

  return +stat.slice(stat.lastIndexOf(')') + 2).split(' ')[16];
}

export function setPriority(pid, priority) {
  if(priority === undefined) [pid, priority] = [0, pid];

  int32(pid, 'pid');
  int32(priority, 'priority', -20, 19);

  /* XXX: TODO */
  throw new InternalError('os.setPriority() is not implemented: no setpriority(2) binding');
}

/* ---- network ---- */

export function networkInterfaces() {
  /* XXX: TODO */
  throw new InternalError('os.networkInterfaces() is not implemented: no getifaddrs(3) binding');
}

/* `${os.hostname}` calls it, as in Node */
for(const fn of [arch, availableParallelism, endianness, freemem, homedir, hostname, platform, release, tmpdir, totalmem, type, uptime, version, machine]) fn[Symbol.toPrimitive] = () => fn();

const os = {
  arch,
  availableParallelism,
  cpus,
  endianness,
  freemem,
  getPriority,
  homedir,
  hostname,
  loadavg,
  networkInterfaces,
  platform,
  release,
  setPriority,
  tmpdir,
  totalmem,
  type,
  userInfo,
  uptime,
  version,
  machine,
};

for(const [name, value] of [
  ['constants', constants],
  ['EOL', EOL],
  ['devNull', devNull],
])
  Object.defineProperty(os, name, { value, enumerable: true, configurable: name != 'constants', writable: false });

export default os;
