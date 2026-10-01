import { SyscallError } from 'syscallerror';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'constructor with no args'() {
    const err = new SyscallError();
    assert(err instanceof SyscallError);
    assert(err instanceof Error);
  },
  'constructor accepts syscall and errno'() {
    const err = new SyscallError('open', SyscallError.ENOENT);
    eq(err.syscall, 'open');
    eq(err.errno, SyscallError.ENOENT);
  },
  'constructor accepts syscall and errno in either order'() {
    const err = new SyscallError(SyscallError.EPERM, 'chmod');
    eq(err.syscall, 'chmod');
    eq(err.errno, SyscallError.EPERM);
  },
  'message reflects the errno via strerror()'() {
    const err = new SyscallError('open', SyscallError.ENOENT);
    assert(typeof err.message === 'string' && err.message.length > 0);
    assert(err.message.includes(SyscallError.strerror(SyscallError.ENOENT)));
  },
  'name is a nonempty string'() {
    const err = new SyscallError('open', SyscallError.ENOENT);
    assert(typeof err.name === 'string' && err.name.length > 0);
  },
  'stack property exists (currently always null - see BUGS)'() {
    /* doc/native/syscallerror.md documents `stack` as "Captured stack
     * trace", but the constructor's captured value never survives to the
     * getter - see BUGS: syscallerror-stack-always-null. */
    const err = new SyscallError('open', SyscallError.ENOENT);
    eq(err.stack, null);
  },
  'toString() renders syscall and errno message'() {
    const err = new SyscallError('open', SyscallError.ENOENT);
    const s = err.toString();
    assert(typeof s === 'string');
    assert(s.includes('open'));
  },
  'Symbol.toPrimitive is aliased to toString()'() {
    const err = new SyscallError('open', SyscallError.ENOENT);
    eq(`${err}`, err.toString());
    eq('' + err, err.toString());
  },
  'static errno() reads and sets the global errno'() {
    const prev = SyscallError.errno();
    SyscallError.errno(SyscallError.EINVAL);
    eq(SyscallError.errno(), SyscallError.EINVAL);
    SyscallError.errno(prev);
  },
  'static strerror() returns the message for a code, defaulting to the current errno'() {
    const msg = SyscallError.strerror(SyscallError.ENOENT);
    assert(typeof msg === 'string' && msg.length > 0);

    SyscallError.errno(SyscallError.EPERM);
    eq(SyscallError.strerror(), SyscallError.strerror(SyscallError.EPERM));
  },
  'errno constants are defined and numeric'() {
    for(const name of ['ENOENT', 'EPERM', 'EINVAL', 'EEXIST', 'EBADF']) {
      assert(typeof SyscallError[name] === 'number', `${name} should be a numeric constant`);
    }
  },
});
