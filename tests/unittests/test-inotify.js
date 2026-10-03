/* lib/inotify.js: inotify(7) wrapper, Linux only */
import { IN_ACCESS, IN_ALL_EVENTS, IN_CREATE, IN_DELETE, IN_MODIFY, IN_MOVED_FROM, IN_MOVED_TO, IN_NONBLOCK, inotify, inotify_add_watch, inotify_close, inotify_event_size, inotify_init, inotify_rm_watch } from '../../lib/inotify.js';
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

const dir = `/tmp/qjsm-test-inotify-${os.getpid()}`;

const sleep = ms => new Promise(resolve => os.setTimeout(resolve, ms));

async function waitFor(predicate, ms = 2000) {
  for(let waited = 0; waited < ms; waited += 10) {
    if(predicate()) return true;
    await sleep(10);
  }

  return predicate();
}

function write(name, text) {
  const f = std.open(`${dir}/${name}`, 'w');

  f.puts(text);
  f.close();
}

tests({
  'setup'() {
    os.mkdir(dir);
  },

  'event masks have the Linux values'() {
    eq(0x1, IN_ACCESS);
    eq(0x2, IN_MODIFY);
    eq(0x100, IN_CREATE);
    eq(0x200, IN_DELETE);
    eq(0x40, IN_MOVED_FROM);
    eq(0x80, IN_MOVED_TO);
    eq(IN_NONBLOCK, 0o4000);
  },

  'IN_ALL_EVENTS includes the single events'() {
    for(const flag of [IN_ACCESS, IN_MODIFY, IN_CREATE, IN_DELETE]) assert(IN_ALL_EVENTS & flag, `flag ${flag}`);
  },

  'inotify_event_size is the size of the fixed event header'() {
    eq(16, inotify_event_size);
  },

  'functional API: init, add_watch, rm_watch, close'() {
    const fd = inotify_init(IN_NONBLOCK);

    assert(fd >= 0, `fd ${fd}`);

    const wd = inotify_add_watch(fd, dir);

    assert(wd > 0, `wd ${wd}`);
    eq(0, inotify_rm_watch(fd, wd));
    inotify_close(fd);
  },

  'adding a missing path throws'() {
    const fd = inotify_init(IN_NONBLOCK);
    let error;

    try {
      inotify_add_watch(fd, '/nonexistent-qjsm-test');
    } catch(e) {
      error = e;
    }

    inotify_close(fd);
    assert(error && /No such file/.test(error.message), String(error));
  },

  async 'a watch reports create, modify and delete with the file name'() {
    const watcher = new inotify();
    const events = [];

    watcher.onread = e => events.push(e);
    watcher.add(dir, IN_CREATE | IN_MODIFY | IN_DELETE);

    try {
      write('a.txt', 'x');
      assert(await waitFor(() => events.some(e => e.mask & IN_CREATE) && events.some(e => e.mask & IN_MODIFY)), 'create/modify');

      os.remove(`${dir}/a.txt`);
      assert(await waitFor(() => events.some(e => e.mask & IN_DELETE)), 'delete');

      for(const e of events) eq('a.txt', e.name);
    } finally {
      watcher.close();
    }
  },

  async 'events carry the watch descriptor and watch(event) maps it back'() {
    const watcher = new inotify();
    const events = [];

    watcher.onread = e => events.push(e);
    watcher.add(dir, IN_CREATE);

    try {
      write('b.txt', 'x');
      assert(await waitFor(() => events.length > 0), 'event');

      const record = watcher.watch(events[0]);

      eq(dir, record.pathname);
      eq(events[0].wd, record.wd);
      eq(IN_CREATE, record.mask);
    } finally {
      watcher.close();
      os.remove(`${dir}/b.txt`);
    }
  },

  'watch() finds a watch by pathname, remove() drops it'() {
    const watcher = new inotify();

    watcher.add(dir);
    eq(dir, watcher.watch(dir).pathname);
    eq(0, watcher.remove(dir));
    eq(undefined, watcher.watch(dir));
    eq(undefined, watcher.remove(dir));
    watcher.close();
  },

  'close() calls onclose'() {
    const watcher = new inotify();
    let closed = false;

    watcher.onclose = () => (closed = true);
    watcher.close();
    eq(true, closed);
  },

  'teardown'() {
    os.exec(['rm', '-rf', dir]);
  },
});
