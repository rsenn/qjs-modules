import { Serial, SerialPort, SerialError } from 'serial';
import { assert, eq, tests } from './tinytest.js';

// This module wraps libserialport, which needs a real (or virtual, e.g.
// socat-created) serial device to open/read/write. Sandboxed CI environments
// generally have neither, and pty pairs created by a background helper
// process aren't reliably visible across process/namespace boundaries here,
// so open()/read()/write()/drain()/getInfo()/getSignals() aren't exercised.
// This suite covers everything that works without real hardware: port
// enumeration, the constants, the request-port error path, and SerialError.

tests({
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

  'SerialError'() {
    const err = new SerialError('boom', SerialPort.ERR_FAIL);

    eq(err.message, 'boom');
    eq(err.type, SerialPort.ERR_FAIL);
    eq(err.name, 'SerialError');
    assert(err instanceof Error);
    eq(Object.prototype.toString.call(err), '[object SerialError]');
  },
});
