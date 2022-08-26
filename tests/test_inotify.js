import * as os from 'os';
import * as std from 'std';
import { Console } from 'console';
import * as misc from 'misc';
import { inotify, IN_ACCESS, IN_ALL_EVENTS, IN_ATTRIB, IN_CLOSE, IN_CLOSE_NOWRITE, IN_CLOSE_WRITE, IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_DONT_FOLLOW, IN_EXCL_UNLINK, IN_IGNORED, IN_ISDIR, IN_MASK_ADD, IN_MODIFY, IN_MOVE, IN_MOVED_FROM, IN_MOVED_TO, IN_MOVE_SELF, IN_ONESHOT, IN_ONLYDIR, IN_OPEN, IN_Q_OVERFLOW, IN_UNMOUNT, IN_NONBLOCK } from '../lib/inotify.js';

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
      colors: true,
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 2,
      showHidden: false,
      customInspect: false
    }
  });

  let watch = new inotify(IN_NONBLOCK);

  watch.add(args[0] ?? '.');

  watch.onread = function(e) {
    let { mask } = e;
    let flags = flags2names(inotifyFlags, mask);
    let w = this.watch(e);
    console.log('inotify event', flags.join('|'), e, w);
  };
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
