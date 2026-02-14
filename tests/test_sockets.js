import * as os from 'os';
import Console from 'console';
import inspect from 'inspect';
import { error } from 'misc';
import { quote } from 'misc';
import { randi } from 'misc';
import { srand } from 'misc';
import { toString } from 'misc';
import { AF_INET, AsyncSocket, fd_set, IPPROTO_TCP, SO_BROADCAST, SO_DEBUG, SO_DONTROUTE, SO_ERROR, SO_KEEPALIVE, SO_OOBINLINE, SO_RCVBUF, SO_REUSEADDR, SO_REUSEPORT, SO_SNDBUF, SOCK_STREAM, SockAddr, Socket, socklen_t, SOL_SOCKET } from 'sockets';

async function main() {
  globalThis.console = new Console({
    inspectOptions: {
      compact: true,
      breakLength: 80,
      maxArrayLength: 100,
      maxStringLength: 100,
    },
  });
  let seed = +Date.now();
  srand(seed);

  let la = new SockAddr(AF_INET, new Uint8Array([192, 168, 8, 151]).buffer, 31337);
  la = new SockAddr(AF_INET, '0.0.0.0', randi() & 0xffff);
  let ra = new SockAddr(AF_INET, '127.0.0.1', 22);
  console.log(`classes`, { socklen_t, fd_set, SockAddr, Socket });
  console.log(`la.toString() =`, la.toString());
  console.log(`ra.toString() =`, ra.toString());
  let opt, ret, data, timeout, pfds, sock;
  console.log('AsyncSocket', AsyncSocket);
  sock = new AsyncSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  console.log('sock', sock);
  //sock.nonblock = false;
  console.log(`sock.nonblock =`, sock.nonblock);

  sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, [1]);
  sock.getsockopt(SOL_SOCKET, SO_REUSEADDR, (opt = []), 4);
  sock.setsockopt(SOL_SOCKET, SO_REUSEPORT, [1]);
  sock.setsockopt(SOL_SOCKET, SO_DEBUG, [1]);

  if(true) {
    const so_flags = Object.entries({
      SO_ERROR,
      SO_DEBUG,
      SO_REUSEPORT,
      SO_REUSEADDR,
      SO_KEEPALIVE,
      SO_DONTROUTE,
      SO_BROADCAST,
      SO_OOBINLINE,
      SO_SNDBUF,
      SO_RCVBUF,
    })
      .sort((a, b) => a[1] - b[1])
      .sort((a, b) => a[0].localeCompare(b[0]));

    for(let [name, value] of so_flags) {
      sock.getsockopt(SOL_SOCKET, value, (opt = [0]), 4);
      console.log(`[${value.toString()}]`.padStart(4), `${name.padEnd(30)} =`, console.config({ compact: true }), opt);
    }
  }
  // console.log(`ndelay(${sock.fd}) =`, sock.ndelay());
  // console.log(`bind(${sock.fd}, `, la, `) =`, sock.bind(la));
  console.log(`connect(${sock.fd}, `, ra, `) =`, await sock.connect(ra), sock.error);

  function DumpSock(s) {
    let { fd, ret, errno, syscall, error, open, eof, mode } = s;

    return [inspect({ fd, ret, errno, syscall, error, open, eof }, {})];
  }

  /*  function ioFlags(flags = 0) {
    let o = [];
    if(flags & POLLIN) o.push('IN');
    if(flags & POLLOUT) o.push('OUT');
    if(flags & POLLERR) o.push('ERR');
    if(flags & POLLHUP) o.push('HUP');
    return o.join('|');
  }

  function PollFD(fd, events = POLLIN) {
    this.fd = fd;
    this.events = events;
    this.revents = 0;
    return this;
  }

  define(PollFD.prototype, {
    [Symbol.inspect]() {
      const { fd, events, revents } = this;
      return `{ fd: ${fd}, events: ${ioFlags(events)}, revents: ${ioFlags(revents)} }`;
    },
    inspect() {
      const { fd, events, revents } = this;
      return `{ fd: ${fd}, events: ${ioFlags(events)}, revents: ${ioFlags(revents)} }`;
    }
  });

  function waitIO(flags = POLLIN) {
    ret = poll((pfds = [new PollFD(sock.fd, flags | POLLERR)]), pfds.length, (timeout = 3000));
  }*/
  console.log('local =', console.config({ compact: true }), sock.local);
  console.log('remote =', console.config({ compact: true }), sock.remote);

  let n,
    buf = new ArrayBuffer(1024);

  while((n = await sock.recv(buf)) > 0) {
    console.log(`n`, n);
    console.log(`buf`, toString(buf));
    let r = await sock.send('\r\n');
    console.log(`r`, r);
  }

  /*os.setWriteHandler(sock.fd, () => {
    os.setWriteHandler(sock.fd, null);

    console.log(`sock.fd = ${sock.fd} connected`);
    console.log(`sock.recv`,sock.recv);

    os.setReadHandler(sock.fd, async () => {
      n = await sock.recv(buf);
      if(n > 0) {
        data = toString(buf, 0, n);
      } else {
        os.setReadHandler(sock.fd, null);
      }

      console.log(`recv(${sock.fd}, ArrayBuffer ${buf.byteLength}) = ${n} ${n >= 0 ? quote(data, "'") : sock.error + ''}`.padEnd(70), ...DumpSock(sock));

      if(n > 0 && data.indexOf('OpenSSH') != -1) {
        const txt = 'BLAHBLAHTEST\r\n';
        let start = 4;
        n = sock.send(txt, start);
        console.log(`send(${quote(txt.slice(start), "'")}, ${start}) =`, n, n > 0 ? null : sock.error, ...DumpSock(sock));
      }
    });
  });*/

  /*  waitIO(POLLOUT);
  for(;;) {
    waitIO(POLLIN);
    if(pfds[0].revents & POLLHUP || n == 0) {
      break;
    }
    if(pfds[0].revents & POLLIN) {
    }
  }*/

  /* console.log(`sock`, ...DumpSock(sock));
  let { open, error } = sock;
  if(error) console.log('error:', error);
  console.log('sock.mode ', sock.mode.toString(2).padStart(16, '0'));

  console.log(('sock.close() ' + sock.close() + '').padEnd(70), ...DumpSock(sock));*/
}

main();
