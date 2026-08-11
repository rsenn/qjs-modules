import { Parser } from '../lib/dom.js';

console.log('=== Testing Document.getElementById ===\n');

// Test 1: Basic getElementById
console.log('Test 1: Basic getElementById');
const parser = new Parser();
const html1 = `<html>
  <body>
    <div id="main">Main content</div>
    <div id="sidebar">Sidebar</div>
  </body>
</html>`;

const doc1 = parser.parseFromString(html1);
const main = doc1.getElementById('main');
console.log('Found main:', main?.tagName, 'id:', main?.getAttribute('id'));
console.log('Pass:', main?.getAttribute('id') === 'main');

const sidebar = doc1.getElementById('sidebar');
console.log('Found sidebar:', sidebar?.tagName, 'id:', sidebar?.getAttribute('id'));
console.log('Pass:', sidebar?.getAttribute('id') === 'sidebar');

// Test 2: Element not found
console.log('\nTest 2: Element not found');
const missing = doc1.getElementById('nonexistent');
console.log('Found nonexistent:', missing);
console.log('Pass:', missing === null);

// Test 3: Nested element
console.log('\nTest 3: Nested element');
const html2 = `<html>
  <body>
    <div id="outer">
      <div id="inner">Nested content</div>
    </div>
  </body>
</html>`;

const doc2 = parser.parseFromString(html2);
const inner = doc2.getElementById('inner');
console.log('Found inner:', inner?.tagName, 'id:', inner?.getAttribute('id'));
console.log('Pass:', inner?.getAttribute('id') === 'inner');

// Test 4: Deeply nested
console.log('\nTest 4: Deeply nested element');
const html3 = `<html>
  <body>
    <div>
      <section>
        <article>
          <p id="deep">Deep content</p>
        </article>
      </section>
    </div>
  </body>
</html>`;

const doc3 = parser.parseFromString(html3);
const deep = doc3.getElementById('deep');
console.log('Found deep:', deep?.tagName, 'id:', deep?.getAttribute('id'));
console.log('Pass:', deep?.getAttribute('id') === 'deep');

// Test 5: Empty or null id
console.log('\nTest 5: Empty or null id');
const empty = doc1.getElementById('');
console.log('Found empty:', empty);
console.log('Pass:', empty === null);

const nullId = doc1.getElementById(null);
console.log('Found null:', nullId);
console.log('Pass:', nullId === null);

// Test 6: Multiple elements with same id (should return first)
console.log('\nTest 6: Multiple elements with same id');
const html4 = `<html>
  <body>
    <div id="duplicate">First</div>
    <div id="duplicate">Second</div>
  </body>
</html>`;

const doc4 = parser.parseFromString(html4);
const first = doc4.getElementById('duplicate');
console.log('Found first:', first?.tagName, 'textContent:', first?.textContent?.trim());
console.log('Pass:', first?.textContent?.trim() === 'First');

console.log('\n✓ All tests completed');
