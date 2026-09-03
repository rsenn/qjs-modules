import { Serial, SerialPort, SerialError } from 'serial';
import { spawnSync } from 'child_process';
import { assert, eq, tests } from './tinytest.js';

/*
This module wraps libserialport, which needs a real (or virtual) serial
device to open/read/write. A socat/pty-created device does NOT work here:
libserialport's Linux backend (get_port_details() in
libserialport/linux.c) requires a /sys/class/tty/<dev>/device entry, which
the kernel never creates for /dev/pts/* pty slaves - sp_get_port_by_name()
fails with SP_ERR_ARG for any pty, regardless of sandboxing. A tty0tty
(https://github.com/freemed/tty0tty) null-modem kernel module pair, e.g.
/dev/tnt0 <-> /dev/tnt1, DOES register under /sys/class/tty and works.

tryProvisionLoopback() below tries to modprobe+chmod its way to a usable
/dev/tnt0+/dev/tnt1 pair (best-effort, via passwordless sudo - sudo -n
fails immediately instead of hanging on a password prompt if that isn't
set up). It never builds/installs the kernel module itself - that has to
happen once, out of band. tty0tty ships its own dkms Debian packaging
(debian/tty0tty-dkms.dkms) - build and install that rather than
hand-writing a dkms.conf (verified working end-to-end this way, starting
from the module not installed at all):

git clone https://github.com/freemed/tty0tty.git
cd tty0tty
sudo apt-get install -y devscripts debhelper dh-dkms
debuild -uc -us
sudo dpkg -i ../tty0tty-dkms_1.2_all.deb

Once installed this way, `modprobe tty0tty` (what tryProvisionLoopback()
below runs) keeps working across reboots and kernel updates without
repeating any of the above - dkms rebuilds the module for each new
kernel automatically.

The open/write/drain/read/getSignals/etc. tests below only run when a
pair ends up present and accessible; otherwise they're skipped with a
note. Port enumeration, the constants, the request-port error path, and
SerialError always run, hardware or not.
*/

function devicesAccessible() {
  try {
    Serial.requestPort('/dev/tnt0').close();
    Serial.requestPort('/dev/tnt1').close();
    return true;
  } catch(e) {
    return false;
  }
}

function tryProvisionLoopback() {
  if(devicesAccessible())
    return true;

  try {
    /* modprobe is a no-op if the module is already loaded; sudo -n fails
       immediately (no hang) if passwordless sudo isn't configured */
    if(spawnSync('sudo', ['-n', 'modprobe', 'tty0tty']).status !== 0)
      return false;

    if(spawnSync('sudo', ['-n', 'chmod', '666', '/dev/tnt0', '/dev/tnt1']).status !== 0)
      return false;
  } catch(e) {
    return false;
  }

  return devicesAccessible();
}

let loopback = null;

if(tryProvisionLoopback()) {
  try {
    const a = Serial.requestPort('/dev/tnt0');
    const b = Serial.requestPort('/dev/tnt1');

    a.open({ baudRate: 9600 });
    b.open({ baudRate: 9600 });

    loopback = { a, b };
  } catch(e) {
    loopback = null;
  }
}

const loopbackTests = loopback
  ? {
      async 'SerialPort write()/read() over a loopback pair'() {
        const { a, b } = loopback;
        const out = new Uint8Array([104, 105]);

        eq(await a.write(out), out.length);
        await a.drain();

        const buf = new Uint8Array(16);
        const n = await b.read(buf);

        eq(n, out.length);
        eq(buf[0], 104);
        eq(buf[1], 105);
      },

      'SerialPort getSignals()/setSignals()'() {
        const { a, b } = loopback;
        const signals = b.getSignals();

        eq(typeof signals.clearToSend, 'boolean');
        eq(typeof signals.dataCarrierDetect, 'boolean');
        eq(typeof signals.dataSetReady, 'boolean');
        eq(typeof signals.ringIndicator, 'boolean');

        a.setSignals({ dataTerminalReady: true, requestToSend: true });
      },

      'SerialPort flush()'() {
        loopback.a.flush();
      },

      'SerialPort inputWaiting/outputWaiting'() {
        const { a } = loopback;

        eq(a.inputWaiting, 0);
        eq(a.outputWaiting, 0);
      },

      'SerialPort getInfo()'() {
        const info = loopback.a.getInfo();

        /* a tty0tty port has no USB/description metadata, so getInfo() may
           legitimately return undefined - just confirm it doesn't throw */
        assert(info === undefined || typeof info === 'object');
      },

      'close the loopback pair (frees /dev/tnt0+/dev/tnt1 for the constructor tests below)'() {
        loopback.a.close();
        loopback.b.close();
      },

      'new SerialPort(path) - unopened, mirrors Serial.requestPort()'() {
        const p = new SerialPort('/dev/tnt0');

        assert(p instanceof SerialPort);
        eq(p.fd, null);
        p.close();
      },

      'new SerialPort(path, options) - auto-opens'() {
        const p = new SerialPort('/dev/tnt0', { baudRate: 9600 });

        eq(typeof p.fd, 'number');
        p.close();
      },

      'new SerialPort({ path, ...options }) - modern Node.js form'() {
        const p = new SerialPort({ path: '/dev/tnt0', baudRate: 19200 });

        eq(typeof p.fd, 'number');
        p.close();
      },

      'new SerialPort(path, { autoOpen: false })'() {
        const p = new SerialPort('/dev/tnt0', { baudRate: 9600, autoOpen: false });

        eq(p.fd, null);
        p.open({ baudRate: 9600 });
        eq(typeof p.fd, 'number');
        p.close();
      },
    }
  : {
      'SerialPort loopback tests'() {
        console.log('  (skipped: no /dev/tnt0+/dev/tnt1 loopback pair - see comment at top of this file)');
      },
    };

await tests({
  'Serial.getPorts()'() {
    const ports = Serial.getPorts();
    assert(Array.isArray(ports));

    for(const name of ports) eq(typeof name, 'string');
  },

  'Serial.requestPort()'() {
    let threw = false;

    try {
      Serial.requestPort('/nonexistent/serial/port/xyz');
    } catch(e) {
      threw = true;
      assert(/not found/.test(e.message));
    }

    assert(threw, 'requestPort() for a nonexistent path should throw');
  },

  'SerialPort static constants'() {
    eq(SerialPort.MODE_READ, 1);
    eq(SerialPort.MODE_WRITE, 2);
    eq(SerialPort.MODE_READ_WRITE, 3);
    eq(SerialPort.BUF_INPUT, 1);
    eq(SerialPort.BUF_OUTPUT, 2);
    eq(SerialPort.BUF_BOTH, 3);
    eq(SerialPort.ERR_ARG, -1);
    eq(SerialPort.ERR_FAIL, -2);
    eq(SerialPort.ERR_MEM, -3);
    eq(SerialPort.ERR_SUPP, -4);
  },

  'new SerialPort() for a nonexistent path throws'() {
    let threw = false;

    try {
      new SerialPort('/nonexistent/serial/port/xyz');
    } catch(e) {
      threw = true;
      assert(/not found/.test(e.message));
    }

    assert(threw, 'new SerialPort() for a nonexistent path should throw');
  },

  'SerialPort.list()'() {
    const list = SerialPort.list();
    assert(Array.isArray(list));

    for(const info of list) {
      eq(typeof info.path, 'string');

      /* manufacturer/serialNumber/vendorId/productId are only present for
         USB devices, so only type-check them when they exist */
      if('vendorId' in info)
        assert(/^[0-9a-f]{4}$/.test(info.vendorId), `vendorId '${info.vendorId}' should be a 4-digit hex string`);

      if('productId' in info)
        assert(/^[0-9a-f]{4}$/.test(info.productId), `productId '${info.productId}' should be a 4-digit hex string`);
    }
  },

  'SerialError'() {
    const err = new SerialError('boom', SerialPort.ERR_FAIL);

    eq(err.message, 'boom');
    eq(err.type, SerialPort.ERR_FAIL);
    eq(err.name, 'SerialError');
    assert(err instanceof Error);
    eq(Object.prototype.toString.call(err), '[object SerialError]');
  },

  ...loopbackTests,
});

if(loopback) {
  loopback.a.close();
  loopback.b.close();
}
