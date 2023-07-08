import * as os from 'os';
import { IN_ACCESS } from '../lib/inotify.js';
import { IN_ATTRIB } from '../lib/inotify.js';
import { IN_CLOSE } from '../lib/inotify.js';
import { IN_CLOSE_NOWRITE } from '../lib/inotify.js';
import { IN_CLOSE_WRITE } from '../lib/inotify.js';
import { IN_CREATE } from '../lib/inotify.js';
import { IN_DELETE } from '../lib/inotify.js';
import { IN_DELETE_SELF } from '../lib/inotify.js';
import { IN_DONT_FOLLOW } from '../lib/inotify.js';
import { IN_EXCL_UNLINK } from '../lib/inotify.js';
import { IN_IGNORED } from '../lib/inotify.js';
import { IN_ISDIR } from '../lib/inotify.js';
import { IN_MASK_ADD } from '../lib/inotify.js';
import { IN_MODIFY } from '../lib/inotify.js';
import { IN_MOVE } from '../lib/inotify.js';
import { IN_MOVE_SELF } from '../lib/inotify.js';
import { IN_MOVED_FROM } from '../lib/inotify.js';
import { IN_MOVED_TO } from '../lib/inotify.js';
import { IN_NONBLOCK } from '../lib/inotify.js';
import { IN_ONESHOT } from '../lib/inotify.js';
import { IN_ONLYDIR } from '../lib/inotify.js';
import { IN_OPEN } from '../lib/inotify.js';
import { IN_Q_OVERFLOW } from '../lib/inotify.js';
import { IN_UNMOUNT } from '../lib/inotify.js';
import { inotify } from '../lib/inotify.js';
import { Console } from 'console';
import * as std from 'std';
('use strict');
('use math');

let inotifyFlags = {
  IN_ACCESS,
  IN_ATTRIB,
  IN_CLOSE,
  IN_CLOSE_NOWRITE,
  IN_CLOSE_WRITE,
  IN_CREATE,
  IN_DELETE,
  IN_DELETE_SELF,
  IN_DONT_FOLLOW,
  IN_EXCL_UNLINK,
  IN_IGNORED,
  IN_ISDIR,
  IN_MASK_ADD,
  IN_MODIFY,
  IN_MOVE,
  IN_MOVED_FROM,
  IN_MOVED_TO,
  IN_MOVE_SELF,
  IN_ONESHOT,
  IN_ONLYDIR,
  IN_OPEN,
  IN_Q_OVERFLOW,
  IN_UNMOUNT,
  IN_NONBLOCK
};

function flags2names(obj, num) {
  let names = [];
  for(let key in obj) {
    if(num & obj[key]) names.push(key);
  }
  return names;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      customInspect: false
    }
  });

  let watch = new inotify(IN_NONBLOCK);
  console.log('watch', watch);

  watch.add(args[0] ?? '.');

  watch.onread = function(e) {
    let { mask } = e;
    console.log('inotify event', console.config({ compact: 0 }), { mask });
    let flags = flags2names(inotifyFlags, mask);
    console.log('inotify event', console.config({ compact: 0 }), flags.join('|'));
    let w = this.watch(e);
    console.log('inotify event', console.config({ compact: 0 }), w);
  };

  os.setTimeout(() => watch.close(), 2000);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}