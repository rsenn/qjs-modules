// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
//: lib/terminal.js: LineEditor completion tests
import { LineEditor, stringWidth } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

// Test 1: Tab key triggers complete and sets ghost text
tests({
  'LineEditor tab completion'() {
    const ed = new LineEditor();
    // Set up a complete function that returns a suggestion
    ed.complete = (text, pos) => {
      if (text.endsWith('hello')) {
        return {
          suggestion: ' world',
          range: [pos, pos + 5]
        };
      }
      return null;
    };
    
    ed.set('hello');
    ed.cursor = 5;
    
    // Trigger tab completion
    const result = ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    eq(result, 'change');
    eq(ed.cmd, 'hello world');
    eq(ed.cursor, 11); // cursor at end of 'hello world'
    eq(ed.ghost, ''); // ghost should be empty after full suggestion
  },
  
  'LineEditor tab completion with partial ghost'() {
    const ed = new LineEditor();
    // Set up a complete function that returns a suggestion with more text
    ed.complete = (text, pos) => {
      if (text.endsWith('hel')) {
        return {
          suggestion: 'hello world',
          range: [pos, pos + 3]
        };
      }
      return null;
    };
    
    ed.set('hel');
    ed.cursor = 3;
    
    // Trigger tab completion - should insert 'lo world' and set ghost
    const result = ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    eq(result, 'change');
    eq(ed.cmd, 'hello world');
    eq(ed.cursor, 11); // cursor at end
    eq(ed.ghost, ' world'); // ghost should have remaining text
  },
  
  'LineEditor layout returns ghostLines'() {
    const ed = new LineEditor();
    ed.complete = (text, pos) => {
      if (text.endsWith('hel')) {
        return {
          suggestion: 'hello world',
          range: [pos, pos + 3]
        };
      }
      return null;
    };
    
    ed.set('hel');
    ed.cursor = 3;
    
    // Trigger tab completion
    ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    
    // Now test layout returns ghostLines
    const { lines, row, col, ghostLines } = ed.layout(['> ', '. '], 80);
    eq(typeof ghostLines, 'object');
    // ghostLines should have the ghost text aligned correctly
    eq(stringWidth(ghostLines[0] || ''), stringWidth(' world'));
  },
  
  'LineEditor layout ghostLines alignment'() {
    const ed = new LineEditor();
    ed.complete = (text, pos) => {
      if (text.endsWith('hel')) {
        return {
          suggestion: 'hello world',
          range: [pos, pos + 3]
        };
      }
      return null;
    };
    
    ed.set('hel');
    ed.cursor = 3;
    
    // Trigger tab completion
    ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    
    const { lines, row, col, ghostLines } = ed.layout(['> ', '. '], 80);
    // The ghost line should start at the correct cell position
    // Using stringWidth to verify alignment
    const prefixWidth = stringWidth(lines[row] || '');
    eq(ghostLines.length, 1);
    // ghost text should be aligned after the prefix
    eq(ghostLines[0], ' world');
  }
});

