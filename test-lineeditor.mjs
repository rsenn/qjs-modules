import { LineEditor, decodeKeys } from './lib/terminal.js';

const ed = new LineEditor();
const { keys } = decodeKeys('hello world', true);
keys.forEach(k => ed.key(k.key));
console.log('cmd:', JSON.stringify(ed.cmd));
console.log('cursor:', ed.cursor);
console.log('killRing:', JSON.stringify(ed.killRing));

// Test delete right
const ed2 = new LineEditor();
ed2.key({ name: 'a', ctrl: false, meta: false, shift: false });
ed2.key({ name: 'b', ctrl: false, meta: false, shift: false });
ed2.key({ name: 'c', ctrl: false, meta: false, shift: false });
ed2.key({ name: 'delete', ctrl: false, meta: false, shift: false });
console.log('delete right - cmd:', JSON.stringify(ed2.cmd), 'cursor:', ed2.cursor);

// Test kill ring
const ed3 = new LineEditor();
ed3.key({ name: 'a', ctrl: false, meta: false, shift: false });
ed3.key({ name: 'b', ctrl: false, meta: false, shift: false });
ed3.key({ name: 'c', ctrl: false, meta: false, shift: false });
ed3.key({ name: 'k', ctrl: true });
console.log('kill ring - cmd:', JSON.stringify(ed3.cmd), 'cursor:', ed3.cursor, 'killRing:', JSON.stringify(ed3.killRing));

// Test Ctrl-U
const ed4 = new LineEditor();
ed4.key({ name: 'a', ctrl: false, meta: false, shift: false });
ed4.key({ name: 'b', ctrl: false, meta: false, shift: false });
ed4.key({ name: 'c', ctrl: false, meta: false, shift: false });
ed4.key({ name: 'u', ctrl: true });
console.log('Ctrl-U - cmd:', JSON.stringify(ed4.cmd), 'cursor:', ed4.cursor, 'killRing:', JSON.stringify(ed4.killRing));
