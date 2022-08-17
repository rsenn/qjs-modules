import { watch, define, error, IN_ACCESS, IN_ALL_EVENTS, IN_ATTRIB, IN_CLOSE, IN_CLOSE_NOWRITE, IN_CLOSE_WRITE, IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_DONT_FOLLOW, IN_EXCL_UNLINK, IN_IGNORED, IN_ISDIR, IN_MASK_ADD, IN_MODIFY, IN_MOVE, IN_MOVED_FROM, IN_MOVED_TO, IN_MOVE_SELF, IN_ONESHOT, IN_ONLYDIR, IN_OPEN, IN_Q_OVERFLOW, IN_UNMOUNT, IN_NONBLOCK } from 'util';
export {
  IN_ACCESS,
  IN_ALL_EVENTS,
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

import { close, setReadHandler, read } from 'os';

export function inotify_init(flags = 0) {
  return watch(flags);
}

export function inotify_add_watch(fd, pathname, mask = IN_ALL_EVENTS) {
  return watch(fd, pathname, mask);
}

export function inotify_rm_watch(fd, wd) {
  return watch(fd, wd);
}

export function inotify_close(fd) {
  return os.close(fd);
}

export class inotify {
  #watches = [];

  constructor(flags = IN_NONBLOCK) {
    this.fd = watch(flags);
    let buf = new ArrayBuffer(1024);
    os.setReadHandler(this.fd, () => {
      let r = os.read(this.fd, buf, 0, buf.byteLength);
      console.log('inotify read', r);
      if(r > 0) {
        let events = watch(buf, 0, r);
        console.log('events', events);
        for(let event of events) {
          /* define(event, {
            get watch() {
              return this.watch(event);
            },
            [Symbol.toStringTag]: 'InotifyEvent'
          });
*/
          this.onread(event);
        }
      } else {
        this.onclose();
        os.close(this.fd);
        os.setReadHandler(this.fd, null);
      }
    });
  }

  add(pathname, mask = IN_ALL_EVENTS) {
    let wd = inotify_add_watch(this.fd, pathname, mask);

    if(wd != -1) {
      this.#watches.push({ wd, pathname, mask });
    } else {
      const { errno } = error();
      this.errno = errno;
      this.onerror(errno);
    }
  }

  watch(wd_or_pathname) {
    if(typeof wd_or_pathname == 'object' && wd_or_pathname != null) wd_or_pathname = wd_or_pathname.wd;

    return this.#watches.find(w => w.wd === wd_or_pathname || w.pathname === wd_or_pathname);
  }

  remove(wd_or_pathname) {
    if(typeof wd_or_pathname == 'object' && wd_or_pathname != null) wd_or_pathname = wd_or_pathname.wd;
    let idx = this.#watches.findIndex(w => w.wd === wd_or_pathname || w.pathname == wd_or_pathname);
    if(idx >= 0) {
      inotify_rm_watch(this.fd, this.#watches[idx].wd);
      this.#watches.splice(idx, 1);
      return idx;
    }
  }

  onread(ev) {}
  onclose() {}
  onerror(errno) {}
}

export default inotify;
