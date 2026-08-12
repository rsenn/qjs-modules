import { inspect } from 'inspect';

const obj = {
  'libmodules.a:x86.S.o': {
    sections: [
      { name: '.text', size: 256, offset: 64, align: 16 },
      { name: '.data', size: 0, offset: 320, align: 1 },
      { name: '.bss', size: 64, offset: 320, align: 8 },
      { name: '.note.GNU-stack', size: 0, offset: 384, align: 1 },
    ],
    symbols: [
      { name: 'main', binding: 'GLOBAL', type: 'FUNC', section: '.text', value: 0 },
      { name: 'helper', binding: 'LOCAL', type: 'FUNC', section: '.text', value: 128 },
      { name: 'buffer', binding: 'GLOBAL', type: 'OBJECT', section: '.bss', value: 0 },
    ],
    relocs: [
      { symbol: 'puts', offset: 44, type: 'R_X86_64_PLT32', addend: -4, section: '.text' },
      { symbol: 'malloc', offset: 80, type: 'R_X86_64_PLT32', addend: -4, section: '.text' },
      { symbol: '.bss', offset: 476, type: 'R_X86_64_PC32', addend: 4, section: '.text' },
    ],
  },
  'libmodules.a:dom.o': {
    sections: [
      { name: '.text', size: 4096, offset: 64, align: 16 },
      { name: '.rodata', size: 512, offset: 4160, align: 4 },
    ],
    symbols: [],
    relocs: [],
  },
};

const opts = { colors: false };

function show(label, compact) {
  console.log(`\n${'─'.repeat(60)}`);
  console.log(` ${label}  (compact: ${compact})`);
  console.log('─'.repeat(60));
  console.log(inspect(obj, { ...opts, compact }));
}

show('Positive compact: count-based (entry limit per object)', 1);
show('Positive compact: only 3 entries fit inline', 3);
show('Negative compact: -1 = compact leaves only', -1);
show('Negative compact: -2 = compact leaves + their parents', -2);
show('Negative compact: -3 = compact leaves + 2 levels up', -3);
