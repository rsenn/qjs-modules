import * as os from 'os';
import { IN_ACCESS } from 'util';
import { IN_ATTRIB } from 'util';
import { IN_CLOSE } from 'util';
import { IN_CLOSE_NOWRITE } from 'util';
import { IN_CLOSE_WRITE } from 'util';
import { IN_CREATE } from 'util';
import { IN_DELETE } from 'util';
import { IN_DELETE_SELF } from 'util';
import { IN_DONT_FOLLOW } from 'util';
import { IN_EXCL_UNLINK } from 'util';
import { IN_IGNORED } from 'util';
import { IN_ISDIR } from 'util';
import { IN_MASK_ADD } from 'util';
import { IN_MODIFY } from 'util';
import { IN_MOVE } from 'util';
import { IN_MOVE_SELF } from 'util';
import { IN_MOVED_FROM } from 'util';
import { IN_MOVED_TO } from 'util';
import { IN_ONESHOT } from 'util';
import { IN_ONLYDIR } from 'util';
import { IN_OPEN } from 'util';
import { IN_Q_OVERFLOW } from 'util';
import { IN_UNMOUNT } from 'util';
import { randStr } from 'util';
import { toArrayBuffer } from 'util';
import { toString } from 'util';
import { watch } from 'util';
import { Console } from 'console';

const modes = Object.entries({
  IN_ACCESS,
  IN_MODIFY,
  IN_ATTRIB,
  IN_CLOSE_WRITE,
  IN_CLOSE_NOWRITE,
  IN_CLOSE,
  IN_OPEN,
  IN_MOVED_FROM,
  IN_MOVED_TO,
  IN_MOVE,
  IN_CREATE,
  IN_DELETE,
  IN_DELETE_SELF,
  IN_MOVE_SELF,
  IN_UNMOUNT,
  IN_Q_OVERFLOW,
  IN_IGNORED,
  IN_ONLYDIR,
  IN_DONT_FOLLOW,
  IN_EXCL_UNLINK,
  IN_MASK_ADD,
  IN_ISDIR,
  IN_ONESHOT
});

const modeNames = Object.fromEntries(modes.map(([name, value]) => [value, name]));

function eventName(n) {
  let out = [];
  for(let [name, value] of modes) {
    if(n & value) out.push(name);
  }
  return out;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      compact: 1
    }
  });
  const file = randStr(10) + '.tmp';
  console.log('file', file);

  const file_fd = os.open(file, os.O_WRONLY | os.O_CREAT, 0o644);
  console.log('file_fd', file_fd);

  const inotify_fd = watch();
  console.log('inotify_fd', inotify_fd);
  const r = watch(inotify_fd, file, 0xfff, (eventType, filename) => console.log('watch event', { eventType, filename }));
  let ev = new Uint32Array(4);
  let ret,
    buf = ev.buffer;
  os.setReadHandler(inotify_fd, () => {
    ret = os.read(inotify_fd, buf, 0, buf.byteLength);
    // console.log('ret', ret);

    if(ret > 0) {
      let namebuf = new ArrayBuffer(ev[3]);
      ret = os.read(inotify_fd, namebuf, 0, ev[3]);
      let name = ret > 0 ? toString(namebuf.slice(0, ret)) : null;

      if(name) console.log('name', name);

      console.log('ev', ev[0], ev[1], modeNames[ev[1]], eventName(ev[1]));
    }
  });

  const data = toArrayBuffer(randStr(1024));
  console.log('os.write() =', os.write(file_fd, data, 0, data.byteLength));
  os.close(file_fd);
  os.setReadHandler(inotify_fd, null);
  os.close(inotify_fd);
}

main(...scriptArgs.slice(1));