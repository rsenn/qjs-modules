import { XMLParser } from 'xml';

/**
 * Convert HTML/XML markup to Markdown, driven by XMLParser's pull events.
 *
 * Handles: h1–h6, p, b/strong, i/em, ul, ol, li, pre, a, img.
 * Only ELEMENT_START, ATTRIBUTE (src/href), TEXT and ELEMENT_END events are used.
 *
 * @param {string} input  HTML/XML source text
 * @returns {string}      Markdown output
 */
export function html2md(input) {
  const p = new XMLParser(input, { tolerant: true, builder: false });
  const stack = [];
  let md = '';

  function headingLevel(tag) {
    const m = /^h([1-6])$/i.exec(tag);
    return m ? +m[1] : 0;
  }

  function top() {
    return stack.length > 0 ? stack[stack.length - 1] : null;
  }
  function parent() {
    return stack.length >= 2 ? stack[stack.length - 2] : null;
  }

  let ev;

  while((ev = p.parse()) > 0) {
    if(ev === XMLParser.ELEMENT_START) {
      const tag = p.eventName;
      const frame = { tag, attrs: {} };
      stack.push(frame);

      const lvl = headingLevel(tag);

      if(lvl) {
        md += '#'.repeat(lvl) + ' ';
      } else if(tag === 'pre') {
        md += '```\n';
      } else if(tag === 'b' || tag === 'strong') {
        md += '**';
      } else if(tag === 'i' || tag === 'em') {
        md += '*';
      } else if(tag === 'li') {
        const par = parent();
        if(par && par.tag === 'ol') {
          par.count = (par.count || 0) + 1;
          md += par.count + '. ';
        } else {
          md += '- ';
        }
      } else if(tag === 'a') {
        md += '[';
      }
      /* p, ul, ol, img: no opening marker needed */
    } else if(ev === XMLParser.ATTRIBUTE) {
      const name = p.eventName;

      if(name === 'src' || name === 'href') {
        const frame = top();
        if(frame) frame.attrs[name] = p.eventValue;
      }
    } else if(ev === XMLParser.TEXT) {
      const frame = top();
      const text = p.eventValue;

      if(frame && frame.tag === 'pre') {
        md += text; /* preserve whitespace inside <pre> */
      } else {
        md += text.replace(/\s+/g, ' '); /* collapse runs of whitespace */
      }
    } else if(ev === XMLParser.ELEMENT_END) {
      const frame = stack.pop();
      if(!frame) continue;

      const tag = frame.tag;
      const lvl = headingLevel(tag);

      if(lvl) {
        md += '\n\n';
      } else if(tag === 'p') {
        md += '\n\n';
      } else if(tag === 'pre') {
        if(!md.endsWith('\n')) md += '\n';
        md += '```\n\n';
      } else if(tag === 'b' || tag === 'strong') {
        md += '**';
      } else if(tag === 'i' || tag === 'em') {
        md += '*';
      } else if(tag === 'li') {
        md += '\n';
      } else if(tag === 'ul' || tag === 'ol') {
        md += '\n';
      } else if(tag === 'a') {
        md += '](' + (frame.attrs.href || '') + ')';
      } else if(tag === 'img') {
        md += '![](' + (frame.attrs.src || '') + ')';
      }
    }
  }

  return md.replace(/\n{3,}/g, '\n\n').trim() + '\n';
}

/* ── demo when run directly ─────────────────────────────────── */

const sample = `
<html>
<body>
  <h1>Welcome</h1>
  <p>This is a <b>bold</b> and <i>italic</i> demo.</p>
  <h2>Links and Images</h2>
  <p>Visit <a href="https://example.com">Example</a> for more.</p>
  <img src="logo.png" />
  <h3>Lists</h3>
  <ul>
    <li>Apples</li>
    <li>Bananas</li>
    <li>Cherries</li>
  </ul>
  <ol>
    <li>First</li>
    <li>Second</li>
    <li>Third</li>
  </ol>
  <h2>Code</h2>
  <pre>function hello() {
  return "world";
}</pre>
</body>
</html>`;

const md = html2md(sample);

import * as std from 'std';
std.out.puts(md);
