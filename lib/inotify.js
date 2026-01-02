import { close } from 'os';
import { read } from 'os';
import { setReadHandler } from 'os';
import { define } from 'util';
import { error } from 'util';
import { watch } from 'util';
import { IN_ACCESS } from 'misc';
import { IN_ALL_EVENTS } from 'misc';
import { IN_ATTRIB } from 'misc';
import { IN_CLOSE } from 'misc';
import { IN_CLOSE_NOWRITE } from 'misc';
import { IN_CLOSE_WRITE } from 'misc';
import { IN_CREATE } from 'misc';
import { IN_DELETE } from 'misc';
import { IN_DELETE_SELF } from 'misc';
import { IN_DONT_FOLLOW } from 'misc';
import { IN_EXCL_UNLINK } from 'misc';
import { IN_IGNORED } from 'misc';
import { IN_ISDIR } from 'misc';
import { IN_MASK_ADD } from 'misc';
import { IN_MODIFY } from 'misc';
import { IN_MOVE } from 'misc';
import { IN_MOVE_SELF } from 'misc';
import { IN_MOVED_FROM } from 'misc';
import { IN_MOVED_TO } from 'misc';
import { IN_NONBLOCK } from 'misc';
import { IN_ONESHOT } from 'misc';
import { IN_ONLYDIR } from 'misc';
import { IN_OPEN } from 'misc';
import { IN_Q_OVERFLOW } from 'misc';
import { IN_UNMOUNT } from 'misc';
import { inotify_event_size } from 'misc';

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
  IN_NONBLOCK,
  inotify_event_size,
};

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
    const buf = new ArrayBuffer(1024);
    let bytes = 0;

    os.setReadHandler(this.fd, () => {
      const r = os.read(this.fd, buf, bytes, buf.byteLength - bytes);
      //console.log('inotify read', r, '/', inotify_event_size);

      if(r > 0) {
        EWOULDBLOCK;

        if(bytes >= inotify_event_size) {
          const events = watch(buf, 0, bytes);
          let seq = 0;

          for(const event of events) {
            define(event, { seq: ++seq });
            this.onread(event);
          }

          bytes = 0;
        }
      } else {
        this.onclose();
        os.close(this.fd);
        os.setReadHandler(this.fd, null);
      }
    });
  }

  add(pathname, mask = IN_ALL_EVENTS) {
    const wd = inotify_add_watch(this.fd, pathname, mask);

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
    const idx = this.#watches.findIndex(w => w.wd === wd_or_pathname || w.pathname == wd_or_pathname);

    if(idx >= 0) {
      inotify_rm_watch(this.fd, this.#watches[idx].wd);
      this.#watches.splice(idx, 1);
      return idx;
    }
  }

  close() {
    this.onclose();
    os.close(this.fd);
    os.setReadHandler(this.fd, null);
  }

  onread(ev) {}
  onclose() {}
  onerror(errno) {}
}

export default inotify;