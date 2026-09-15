import { AbortController } from 'abort';
import { mkdirSync, watch } from 'fs';
import { watch as watchPromises } from 'fsPromises';
import { setTimeout } from 'timers';
import * as os from 'os';
import { assert, eq, tests } from './tinytest.js';

const TMP = '.tmp/test_fs_watch';

function ensureDir() {
  try {
    mkdirSync(TMP);
  } catch(e) {}
}

function writeFile(path) {
  const fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644);
  os.write(fd, new Uint8Array([1, 2, 3]).buffer, 0, 3);
  os.close(fd);
}

function removeQuiet(path) {
  try {
    os.remove(path);
  } catch(e) {}
}

function waitFor(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

tests({
  async 'fs.watch() reports rename on create and change on write, then stops after close'() {
    ensureDir();
    const file = TMP + '/a.txt';
    removeQuiet(file);

    const events = [];
    const w = watch(TMP, {}, (eventType, filename) => events.push([eventType, filename]));

    await waitFor(50);
    writeFile(file);
    await waitFor(150);

    w.close();
    const countAfterClose = events.length;

    /* No more events should show up after close(), even if we keep writing. */
    writeFile(file);
    await waitFor(150);

    removeQuiet(file);

    assert(events.length > 0, 'expected at least one event, got none');
    assert(
      events.some(([type, name]) => type === 'rename' && name === 'a.txt'),
      `expected a 'rename' event for a.txt, got ${JSON.stringify(events)}`,
    );
    assert(
      events.some(([type, name]) => type === 'change' && name === 'a.txt'),
      `expected a 'change' event for a.txt, got ${JSON.stringify(events)}`,
    );
    eq(countAfterClose, events.length);
  },

  async 'fs.watch() also emits change/rename on the returned FSWatcher'() {
    ensureDir();
    const file = TMP + '/b.txt';
    removeQuiet(file);

    const seen = [];
    const w = watch(TMP);
    w.on('rename', (eventType, filename) => seen.push(filename));

    await waitFor(50);
    writeFile(file);
    await waitFor(150);
    w.close();

    removeQuiet(file);

    assert(seen.includes('b.txt'), `expected FSWatcher 'rename' event for b.txt, got ${JSON.stringify(seen)}`);
  },

  async "fs.watch() closes when options.signal aborts, and emits 'close'"() {
    ensureDir();

    const ac = new AbortController();
    const w = watch(TMP, { signal: ac.signal });

    let closed = false;
    w.on('close', () => (closed = true));

    ac.abort();
    await waitFor(10);

    assert(closed, "expected 'close' to have fired after signal abort");
  },

  async 'fsPromises.watch() yields {eventType, filename} and stops on abort'() {
    ensureDir();
    const file = TMP + '/c.txt';
    removeQuiet(file);

    const ac = new AbortController();
    const events = [];

    setTimeout(() => writeFile(file), 50);
    setTimeout(() => ac.abort(), 250);

    for await(const ev of watchPromises(TMP, { signal: ac.signal })) events.push(ev);

    removeQuiet(file);

    assert(events.length > 0, 'expected at least one event from fsPromises.watch()');
    assert(
      events.some(ev => ev.eventType === 'rename' && ev.filename === 'c.txt'),
      `expected a rename event for c.txt, got ${JSON.stringify(events)}`,
    );
  },
});
