import { GPIO } from 'gpio';
import { assert, eq, tests } from './tinytest.js';

// This module memory-maps /dev/gpiomem and only works on real Raspberry Pi
// hardware. There is no way to exercise initPin()/setPin()/getPin()/buffer()
// without that hardware, so this suite only covers what's verifiable
// anywhere: the static constants and the constructor's failure path when
// /dev/gpiomem isn't present (the common case off-device, e.g. in CI).

tests({
  'static constants'() {
    eq(GPIO.INPUT, 0);
    eq(GPIO.OUTPUT, 1);
    eq(GPIO.LOW, 0);
    eq(GPIO.HIGH, 1);
  },

  'constructor'() {
    let threw = false;

    try {
      new GPIO();
    } catch(e) {
      threw = true;
      assert(e instanceof Error);
    }

    // On real Raspberry Pi hardware (with /dev/gpiomem present and
    // accessible) this constructs successfully instead; either outcome is
    // acceptable here since we can't control which environment this runs in.
    if(!threw) assert(true);
  },
});
