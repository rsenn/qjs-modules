import * as os from 'os';
import * as io from 'io';
import { toString } from 'misc';
import { AF_INET, AF_INET6, AF_UNIX, AsyncSocket, ECONNREFUSED, IPPROTO_TCP, IPPROTO_UDP, POLLIN, POLLOUT, SHUT_RD, SHUT_RDWR, SHUT_WR, SO_ERROR, SO_REUSEADDR, SO_TYPE, SOCK_DGRAM, SOCK_STREAM, SockAddr, Socket, SOL_SOCKET, SyscallError, poll, select, socketpair, } from 'sockets';
import { assert, eq, tests } from './tinytest.js';

/* the async methods resolve via globalThis.io.set{Read,Write}Handler() */
globalThis.io = io;

const UNIX_PATH = 'tests/.test_sockets.sock';

function loopback(port = 0) {
  return new SockAddr(AF_INET, '127.0.0.1', port);
}

/* create a bound + listening TCP server on an ephemeral port */
function tcpServer(ctor = Socket) {
  const srv = new ctor(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  srv.setsockopt(SOL_SOCKET, SO_REUSEADDR, [1]);
  srv.bind(loopback());
  srv.listen(5);
  return srv;
}

function throws(fn, check) {
  let caught = null;
  try {
    fn();
  } catch(e) {
    caught = e;
  }
  assert(caught, 'expected an exception');
  if(check) check(caught);
  return caught;
}

tests({
  /* ======================== SockAddr ======================== */

  'new SockAddr(family, addr, port)'() {
    const a = new SockAddr(AF_INET, '10.20.30.40', 8080);
    eq(a.family, AF_INET);
    eq(a.addr, '10.20.30.40');
    eq(a.port, 8080);
    eq(a.toString(), '10.20.30.40:8080');
    eq(Object.prototype.toString.call(a), '[object SockAddr]');
  },

  'SockAddr family autodetection'() {
    const v4 = new SockAddr('192.168.1.1', 80);
    eq(v4.family, AF_INET);
    eq(v4.toString(), '192.168.1.1:80');

    const v6 = new SockAddr('::1', 443);
    eq(v6.family, AF_INET6);
    eq(v6.addr, '::1');
    eq(v6.port, 443);
  },

  'SockAddr copy constructor'() {
    const a = new SockAddr(AF_INET, '1.2.3.4', 99);
    const b = new SockAddr(a);
    eq(b.toString(), '1.2.3.4:99');
    b.port = 100;
    eq(a.port, 99);
  },

  'new SockAddr(ArrayBuffer)'() {
    const a = new SockAddr(AF_INET, '1.2.3.4', 99);
    const b = new SockAddr(a.buffer.slice(0));
    eq(b.family, AF_INET);
    eq(b.toString(), '1.2.3.4:99');
  },

  'SockAddr AF_UNIX path'() {
    const u = new SockAddr(AF_UNIX, UNIX_PATH);
    eq(u.family, AF_UNIX);
    eq(u.path, UNIX_PATH);
    eq(u.toString(), 'unix://' + UNIX_PATH);
    eq(u.port, undefined);

    u.path = 'other.sock';
    eq(u.path, 'other.sock');
  },

  'SockAddr addr/port setters'() {
    const a = new SockAddr(AF_INET, '10.0.0.1', 1);
    a.addr = '1.2.3.4';
    a.port = 65535;
    eq(a.addr, '1.2.3.4');
    eq(a.port, 65535);
  },

  'SockAddr buffer/byteLength'() {
    const a = new SockAddr(AF_INET, '127.0.0.1', 1234);
    assert(a.buffer instanceof ArrayBuffer, '.buffer should be an ArrayBuffer');
    eq(a.buffer.byteLength, a.byteLength);
    assert(a.byteLength >= 8, 'sockaddr_in should be at least 8 bytes');
  },

  'SockAddr clone()'() {
    const a = new SockAddr(AF_INET, '5.6.7.8', 55);
    const c = a.clone();
    eq(c.toString(), '5.6.7.8:55');
    c.port = 66;
    eq(a.port, 55);
    eq(c.port, 66);
  },

  /* ======================== Socket (sync) ======================== */

  'new Socket() properties'() {
    const s = new Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(s.fd >= 0, 'fd should be a valid descriptor');
    eq(s.open, true);
    eq(s.eof, false);
    eq(s.nonblock, false);
    eq(typeof s.mode, 'number');
    eq(Object.prototype.toString.call(s), '[object Socket]');
    eq(+s, s.fd); /* valueOf / Symbol.toPrimitive */
    s.close();
  },

  'Socket constructor argument errors'() {
    throws(
      () => new Socket(),
      e => assert(e instanceof TypeError, 'expected TypeError'),
    );
    throws(
      () => new Socket('foo'),
      e => assert(e instanceof TypeError, 'expected TypeError'),
    );
    throws(
      () => new Socket(AF_INET, 'bar'),
      e => assert(e instanceof TypeError, 'expected TypeError'),
    );
  },

  'getsockopt()/setsockopt()'() {
    const s = new Socket(AF_INET, SOCK_STREAM);
    const opt = [0];

    s.getsockopt(SOL_SOCKET, SO_TYPE, opt);
    eq(opt[0], SOCK_STREAM);

    s.getsockopt(SOL_SOCKET, SO_ERROR, opt);
    eq(opt[0], 0);

    s.setsockopt(SOL_SOCKET, SO_REUSEADDR, [1]);
    s.getsockopt(SOL_SOCKET, SO_REUSEADDR, opt);
    eq(opt[0], 1);

    /* optval as ArrayBuffer instead of array */
    s.setsockopt(SOL_SOCKET, SO_REUSEADDR, new Uint32Array([0]).buffer);
    s.getsockopt(SOL_SOCKET, SO_REUSEADDR, opt);
    eq(opt[0], 0);

    s.close();
  },

  'TCP bind/listen/connect/accept/send/recv'() {
    const srv = tcpServer();
    const local = srv.local;
    eq(local.addr, '127.0.0.1');
    assert(local.port > 0, 'bound port should be assigned');

    const cli = new Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    eq(cli.connect(loopback(local.port)), 0);

    const peer = new SockAddr(AF_INET);
    const conn = srv.accept(peer);
    assert(conn instanceof Socket, 'accept() should return a Socket');
    assert(conn.fd >= 0 && conn.fd != srv.fd, 'accepted socket should have its own fd');
    eq(peer.addr, '127.0.0.1');
    eq(peer.port, cli.local.port);

    /* local/remote of the two connection endpoints match up */
    eq(conn.remote.toString(), cli.local.toString());
    eq(conn.local.toString(), cli.remote.toString());

    eq(cli.send('hello'), 5);
    const buf = new ArrayBuffer(16);
    const n = conn.recv(buf);
    eq(n, 5);
    eq(toString(buf, 0, n), 'hello');

    /* and the other direction */
    eq(conn.send('world!'), 6);
    eq(cli.recv(buf), 6);
    eq(toString(buf, 0, 6), 'world!');

    conn.close();
    cli.close();
    srv.close();
  },

  'shutdown() / eof'() {
    const srv = tcpServer();
    const cli = new Socket(AF_INET, SOCK_STREAM);
    cli.connect(loopback(srv.local.port));
    const conn = srv.accept(new SockAddr(AF_INET));

    cli.shutdown(SHUT_WR);
    const buf = new ArrayBuffer(8);
    eq(conn.recv(buf), 0); /* orderly shutdown -> EOF */
    eq(conn.eof, true);

    conn.shutdown(SHUT_RDWR);
    eq(cli.recv(buf), 0);

    conn.close();
    cli.close();
    srv.close();
  },

  'close() marks socket closed, further use throws'() {
    const s = new Socket(AF_INET, SOCK_STREAM);
    eq(s.close(), undefined);
    eq(s.open, false);
    eq(s.fd, -1);

    throws(
      () => s.close(),
      e => {
        assert(/closed/.test(e.message), 'expected "already been closed", got: ' + e.message);
      },
    );
    throws(() => s.send('x'));
    throws(() => s.recv(new ArrayBuffer(4)));
  },

  'send()/recv() with offset and length'() {
    const sv = [];
    eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    const a = Socket.adopt(sv[0]),
      b = Socket.adopt(sv[1]);

    /* send('abcdef', offset = 2, length = 3) -> 'cde' */
    eq(a.send('abcdef', 2, 3), 3);
    const buf = new ArrayBuffer(8);
    eq(b.recv(buf), 3);
    eq(toString(buf, 0, 3), 'cde');

    /* recv into the middle of a buffer */
    a.send('XY');
    eq(b.recv(buf, 4, 2), 2);
    eq(toString(buf, 4, 2), 'XY');

    a.close();
    b.close();
  },

  'UDP sendto()/recvfrom()'() {
    const u1 = new Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const u2 = new Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u1.bind(loopback());
    u2.bind(loopback());

    eq(u2.sendto('datagram', 0, 8, 0, u1.local), 8);

    const buf = new ArrayBuffer(64);
    const from = new SockAddr(AF_INET);
    const n = u1.recvfrom(buf, 0, 64, 0, from);
    eq(n, 8);
    eq(toString(buf, 0, n), 'datagram');
    eq(from.toString(), u2.local.toString());

    u1.close();
    u2.close();
  },

  'UDP connect()ed send/recv'() {
    const u1 = new Socket(AF_INET, SOCK_DGRAM);
    const u2 = new Socket(AF_INET, SOCK_DGRAM);
    u1.bind(loopback());
    eq(u2.connect(u1.local), 0);

    eq(u2.send('ping'), 4);
    const buf = new ArrayBuffer(8);
    eq(u1.recv(buf), 4);
    eq(toString(buf, 0, 4), 'ping');

    u1.close();
    u2.close();
  },

  'UNIX socket bind/connect/accept'() {
    os.remove(UNIX_PATH);

    const srv = new Socket(AF_UNIX, SOCK_STREAM);
    const addr = new SockAddr(AF_UNIX, UNIX_PATH);
    eq(srv.bind(addr), undefined);
    eq(srv.listen(), undefined);
    eq(srv.local.path, UNIX_PATH);

    const cli = new Socket(AF_UNIX, SOCK_STREAM);
    eq(cli.connect(new SockAddr(AF_UNIX, UNIX_PATH)), 0);

    const peer = new SockAddr(AF_UNIX);
    const conn = srv.accept(peer);
    eq(cli.send('unix!'), 5);
    const buf = new ArrayBuffer(8);
    eq(conn.recv(buf), 5);
    eq(toString(buf, 0, 5), 'unix!');

    conn.close();
    cli.close();
    srv.close();
    os.remove(UNIX_PATH);
  },

  'socketpair() + Socket.adopt()'() {
    const sv = [];
    eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    eq(sv.length, 2);
    assert(sv[0] >= 0 && sv[1] >= 0, 'socketpair should yield two descriptors');

    const a = Socket.adopt(sv[0]),
      b = Socket.adopt(sv[1]);
    assert(a instanceof Socket, 'adopt() should return a Socket');
    eq(a.fd, sv[0]);
    eq(a.open, true);

    eq(a.send('ping'), 4);
    const buf = new ArrayBuffer(8);
    eq(b.recv(buf), 4);
    eq(toString(buf, 0, 4), 'ping');

    a.close();
    b.close();
  },

  'ndelay() / nonblock: sync socket refuses nonblocking IO'() {
    const s = new Socket(AF_INET, SOCK_STREAM);
    eq(s.nonblock, false);
    s.ndelay(true);
    eq(s.nonblock, true);

    /* nonblocking IO on the synchronous Socket class is rejected;
       AsyncSocket must be used instead */
    throws(
      () => s.recv(new ArrayBuffer(4)),
      e => {
        assert(/wait/.test(e.message), 'expected wait assert, got: ' + e.message);
      },
    );

    s.ndelay(false);
    eq(s.nonblock, false);
    s.close();
  },

  'sendmsg()/recvmsg()'() {
    const sv = [];
    eq(socketpair(AF_UNIX, SOCK_DGRAM, 0, sv), 0);
    const a = Socket.adopt(sv[0]),
      b = Socket.adopt(sv[1]);

    /* iov as plain array of chunks */
    eq(a.sendmsg(['hello', ' ', 'world']), 11);

    /* scatter into two buffers */
    const b1 = new ArrayBuffer(6),
      b2 = new ArrayBuffer(16);
    const n = b.recvmsg([b1, b2]);
    eq(n, 11);
    eq(toString(b1), 'hello ');
    eq(toString(b2, 0, n - 6), 'world');

    /* iov inside an object */
    eq(a.sendmsg({ iov: ['obj'] }), 3);
    const b3 = new ArrayBuffer(8);
    eq(b.recvmsg([b3]), 3);
    eq(toString(b3, 0, 3), 'obj');

    a.close();
    b.close();
  },

  'select()'() {
    const sv = [];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    const a = Socket.adopt(sv[0]),
      b = Socket.adopt(sv[1]);

    /* nothing readable yet -> timeout, readable set cleared */
    let rfds = [b.fd];
    eq(select(b.fd + 1, rfds, [], [], 10), 0);
    eq(rfds.length, 0);

    a.send('x');
    rfds = [b.fd];
    const wfds = [a.fd];
    const n = select(Math.max(a.fd, b.fd) + 1, rfds, wfds, [], 1000);
    assert(n >= 2, 'expected read + write readiness, got ' + n);
    eq(rfds.length, 1);
    eq(rfds[0], b.fd);
    eq(wfds[0], a.fd);

    a.close();
    b.close();
  },

  'poll()'() {
    const sv = [];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    const a = Socket.adopt(sv[0]),
      b = Socket.adopt(sv[1]);

    /* timeout, no events */
    eq(poll([{ fd: b.fd, events: POLLIN, revents: 0 }], 1, 10), 0);

    a.send('x');

    /* object form */
    const pfd = { fd: b.fd, events: POLLIN, revents: 0 };
    eq(poll([pfd], 1, 1000), 1);
    assert(pfd.revents & POLLIN, 'POLLIN should be set');

    /* array form: [fd, events, revents] */
    const arr = [a.fd, POLLOUT, 0];
    eq(poll([arr], 1, 1000), 1);
    assert(arr[2] & POLLOUT, 'POLLOUT should be set');

    a.close();
    b.close();
  },

  'connect() refused throws SyscallError'() {
    /* bind a listener, take its port, close it -> port is refused */
    const srv = tcpServer();
    const port = srv.local.port;
    srv.close();

    const s = new Socket(AF_INET, SOCK_STREAM);
    throws(
      () => s.connect(loopback(port)),
      e => {
        assert(e instanceof SyscallError, 'expected SyscallError, got: ' + e);
        eq(e.syscall, 'connect');
        eq(e.errno, ECONNREFUSED);
      },
    );
    s.close();
  },

  'bind() on privileged/invalid address throws SyscallError'() {
    const s = new Socket(AF_INET, SOCK_STREAM);
    /* non-local address cannot be bound */
    throws(
      () => s.bind(new SockAddr(AF_INET, '203.0.113.1', 0)),
      e => {
        assert(e instanceof SyscallError, 'expected SyscallError, got: ' + e);
        eq(e.syscall, 'bind');
      },
    );
    s.close();
  },

  'bind()/connect() without address throw TypeError'() {
    const s = new Socket(AF_INET, SOCK_STREAM);
    throws(
      () => s.bind(),
      e => assert(e instanceof TypeError, 'expected TypeError'),
    );
    throws(
      () => s.connect(),
      e => assert(e instanceof TypeError, 'expected TypeError'),
    );
    s.close();
  },

  /* ======================== AsyncSocket ======================== */

  async 'AsyncSocket TCP connect/accept/send/recv'() {
    const srv = tcpServer(AsyncSocket);
    eq(srv.nonblock, true);
    const port = srv.local.port;

    const cli = new AsyncSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const connecting = cli.connect(loopback(port));

    const peer = new SockAddr(AF_INET);
    const conn = await srv.accept(peer);
    assert(conn instanceof AsyncSocket, 'accepted socket should be an AsyncSocket');
    eq(conn.nonblock, true);
    eq(peer.port, cli.local.port);

    eq(await connecting, undefined);

    eq(await cli.send('async hello'), 11);
    const buf = new ArrayBuffer(64);
    const n = await conn.recv(buf);
    eq(n, 11);
    eq(toString(buf, 0, n), 'async hello');

    /* echo back */
    eq(await conn.send(buf, 0, n), 11);
    eq(await cli.recv(buf), 11);

    /* EOF after peer shutdown */
    cli.shutdown(SHUT_WR);
    eq(await conn.recv(buf), 0);

    conn.close();
    cli.close();
    srv.close();
  },

  async 'AsyncSocket UDP sendto/recvfrom'() {
    const u1 = new AsyncSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const u2 = new AsyncSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u1.bind(loopback());
    u2.bind(loopback());

    eq(await u2.sendto('dgram', 0, 5, 0, u1.local), 5);

    const buf = new ArrayBuffer(16);
    const from = new SockAddr(AF_INET);
    const n = await u1.recvfrom(buf, 0, 16, 0, from);
    eq(n, 5);
    eq(toString(buf, 0, n), 'dgram');
    eq(from.toString(), u2.local.toString());

    u1.close();
    u2.close();
  },

  async 'AsyncSocket connect() refused rejects'() {
    const srv = tcpServer();
    const port = srv.local.port;
    srv.close();

    const s = new AsyncSocket(AF_INET, SOCK_STREAM);
    let caught = null;
    try {
      await s.connect(loopback(port));
    } catch(e) {
      caught = e;
    }
    assert(caught, 'connect to closed port should reject');
    eq(caught.syscall, 'connect');
    eq(caught.errno, ECONNREFUSED);
    s.close();
  },

  async 'AsyncSocket rejects a second concurrent recv()'() {
    const sv = [];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    const a = Socket.adopt(sv[0], true),
      b = Socket.adopt(sv[1], true);
    assert(a instanceof AsyncSocket, 'adopt(fd, true) should return an AsyncSocket');

    const first = b.recv(new ArrayBuffer(8));

    let caught = null;
    try {
      await b.recv(new ArrayBuffer(8));
    } catch(e) {
      caught = e;
    }
    assert(caught, 'second concurrent recv() should reject');
    assert(/pending/.test(caught.message), 'expected pending read error, got: ' + caught.message);

    await a.send('done');
    eq(await first, 4);

    a.close();
    b.close();
  },

  async 'AsyncSocket UNIX socketpair send/recv'() {
    const sv = [];
    eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    const a = Socket.adopt(sv[0], true),
      b = Socket.adopt(sv[1], true);

    eq(await a.send('over unix'), 9);
    const buf = new ArrayBuffer(16);
    const n = await b.recv(buf);
    eq(n, 9);
    eq(toString(buf, 0, n), 'over unix');

    a.close();
    b.close();
  },
});
