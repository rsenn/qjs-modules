import { watch, IN_ACCESS, IN_ALL_EVENTS, IN_ATTRIB, IN_CLOSE, IN_CLOSE_NOWRITE, IN_CLOSE_WRITE, IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_DONT_FOLLOW, IN_EXCL_UNLINK, IN_IGNORED, IN_ISDIR, IN_MASK_ADD, IN_MODIFY, IN_MOVE, IN_MOVED_FROM, IN_MOVED_TO, IN_MOVE_SELF, IN_ONESHOT, IN_ONLYDIR, IN_OPEN, IN_Q_OVERFLOW, IN_UNMOUNT, IN_NONBLOCK } from 'util';
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

export class inotify {}

export default inotify;
