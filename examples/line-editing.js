#!/usr/bin/env qjsm
import { LineEditor, decodeKeys, stringWidth } from 'terminal';

// Helper to simulate key presses
function key(name, ctrl = false, meta = false, shift = false) {
  return { name, ctrl, meta, shift };
}

// Demo 1: Basic typing
function demoBasicTyping() {
  const ed = new LineEditor();
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c'));
  if (ed.cmd !== 'abc' || ed.cursor !== 3) {
    throw new Error(`Demo 1 failed: expected "abc" cursor 3, got "${ed.cmd}" cursor ${ed.cursor}`);
  }
  console.log('Demo 1 PASSED: Basic typing');
}

// Demo 2: Backspace
function demoBackspace() {
  const ed = new LineEditor();
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c'));
  ed.key(key('backspace'));
  if (ed.cmd !== 'ab' || ed.cursor !== 2) {
    throw new Error(`Demo 2 failed: expected "ab" cursor 2, got "${ed.cmd}" cursor ${ed.cursor}`);
  }
  console.log('Demo 2 PASSED: Backspace');
}

// Demo 3: History navigation
function demoHistory() {
  const ed = new LineEditor({ history: ['git status', 'npm test', 'make build'] });
  
  ed.key(key('up'));
  if (ed.cmd !== 'make build') {
    throw new Error(`Demo 3 failed up: expected "make build", got "${ed.cmd}"`);
  }
  
  ed.key(key('up'));
  if (ed.cmd !== 'npm test') {
    throw new Error(`Demo 3 failed up2: expected "npm test", got "${ed.cmd}"`);
  }
  
  ed.key(key('up'));
  if (ed.cmd !== 'git status') {
    throw new Error(`Demo 3 failed up3: expected "git status", got "${ed.cmd}"`);
  }
  
  ed.key(key('down'));
  if (ed.cmd !== 'npm test') {
    throw new Error(`Demo 3 failed down: expected "npm test", got "${ed.cmd}"`);
  }
  
  ed.key(key('down'));
  if (ed.cmd !== 'make build') {
    throw new Error(`Demo 3 failed down2: expected "make build", got "${ed.cmd}"`);
  }
  
  if (ed.history.length !== 3) {
    throw new Error(`Demo 3 failed: expected history length 3, got ${ed.history.length}`);
  }
  console.log('Demo 3 PASSED: History navigation');
}

// Demo 4: Kill ring
function demoKillRing() {
  const ed = new LineEditor();
  
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c'));
  
  ed.key(key('left'));
  ed.key(key('k', true));  // Ctrl-K kills to end
  
  if (ed.cmd !== 'ab' || ed.killRing !== 'c') {
    throw new Error(`Demo 4 failed after Ctrl-K: expected cmd="ab" killRing="c", got cmd="${ed.cmd}" killRing="${ed.killRing}"`);
  }
  
  // Yank back
  ed.key(key('y', true));  // Ctrl-Y yanks back
  if (ed.cmd !== 'abc') {
    throw new Error(`Demo 4 failed after Ctrl-Y: expected "abc", got "${ed.cmd}"`);
  }
  console.log('Demo 4 PASSED: Kill ring');
}

// Demo 5: Ctrl-U kill to start
function demoCtrlU() {
  const ed = new LineEditor();
  
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c'));
  
  ed.key(key('u', true));  // Ctrl-U kills to start
  
  if (ed.cmd !== '' || ed.killRing !== 'abc') {
    throw new Error(`Demo 5 failed: expected cmd="" killRing="abc", got cmd="${ed.cmd}" killRing="${ed.killRing}"`);
  }
  console.log('Demo 5 PASSED: Ctrl-U kill to start');
}

// Demo 6: Ctrl-W kill word
function demoCtrlW() {
  const ed = new LineEditor();
  
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c'));
  ed.key(key('d'));
  
  ed.key(key('left'));
  ed.key(key('left'));
  
  ed.key(key('w', true));  // Ctrl-W kills word before cursor
  
  if (ed.cmd !== 'cd' || ed.killRing !== 'ab' || ed.cursor !== 0) {
    throw new Error(`Demo 6 failed: expected cmd="cd" killRing="ab" cursor=0, got cmd="${ed.cmd}" killRing="${ed.killRing}" cursor=${ed.cursor}`);
  }
  console.log('Demo 6 PASSED: Ctrl-W kill word');
}

// Demo 7: Tab completion
function demoTabCompletion() {
  const ed = new LineEditor();
  ed.set('hello world');
  
  ed.complete = (text, pos) => ({ suggestion: 'there', range: [6, 11] });
  ed.key(key('tab'));
  
  if (ed.cmd !== 'hello there' || ed.cursor !== 11) {
    throw new Error(`Demo 7 failed: expected cmd="hello there" cursor=11, got cmd="${ed.cmd}" cursor=${ed.cursor}`);
  }
  console.log('Demo 7 PASSED: Tab completion');
}

// Demo 8: Reverse search (Ctrl-R)
function demoReverseSearch() {
  const ed = new LineEditor({ history: ['hello world', 'goodbye world', 'hello qjs'] });
  
  ed.key(key('ctrlR'));  // Start reverse search
  
  // Type 'hello'
  ed.key(key('h'));
  ed.key(key('e'));
  ed.key(key('l'));
  ed.key(key('l'));
  ed.key(key('o'));
  
  if (ed.searchBuffer !== 'hello' || ed.searchIndex !== 2) {
    throw new Error(`Demo 8 failed search: expected searchBuffer="hello" searchIndex=2, got searchBuffer="${ed.searchBuffer}" searchIndex=${ed.searchIndex}`);
  }
  
  // Navigate to older match or accept with Enter
  ed.key(key('enter'));
  if (ed.cmd !== 'hello qjs') {
    throw new Error(`Demo 8 failed enter: expected cmd="hello qjs", got cmd="${ed.cmd}"`);
  }
  console.log('Demo 8 PASSED: Reverse search');
}

// Demo 9: Multi-line editing
function demoMultiLine() {
  const ed = new LineEditor();
  
  ed.key(key('a'));
  ed.key(key('b'));
  
  const { lines } = ed.layout(['> ', '. '], 80);
  if (lines.join('|') !== '> ab') {
    throw new Error(`Demo 9 failed: expected lines "> ab", got "${lines.join('|')}"`);
  }
  console.log('Demo 9 PASSED: Multi-line layout');
}

// Demo 10: Ctrl-C clear
function demoCtrlC() {
  const ed = new LineEditor();
  
  ed.key(key('a'));
  ed.key(key('b'));
  ed.key(key('c', true));  // Ctrl-C
  
  if (ed.cmd !== '' || ed.accept() !== '') {
    throw new Error(`Demo 10 failed: expected cmd="" accept="", got cmd="${ed.cmd}" accept="${ed.accept()}"`);
  }
  console.log('Demo 10 PASSED: Ctrl-C clear');
}

// Run all demos
console.log('LineEditor Demo Program');
console.log('=======================\n');

demoBasicTyping();
demoBackspace();
demoHistory();
demoKillRing();
demoCtrlU();
demoCtrlW();
demoTabCompletion();
demoReverseSearch();
demoMultiLine();
demoCtrlC();

console.log('\n=== All demos passed ===');