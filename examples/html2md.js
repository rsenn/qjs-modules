#!/usr/bin/env qjsm
import * as std from 'std';
import { isMainModule } from 'util';
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

  const INLINE_TAGS = new Set(['b', 'strong', 'i', 'em', 'a', 'img', 'code', 'span']);

  function ensureSpaceBefore() {
    if(md.length > 0 && !/\s/.test(md[md.length - 1])) md += ' ';
  }

  function ensureSpaceAfter() {
    md += ' ';
  }

  function top() {
    return stack.length > 0 ? stack[stack.length - 1] : null;
  }
  function parent() {
    return stack.length >= 2 ? stack[stack.length - 2] : null;
  }

  let ev;

  while((ev = p.parse()) > 0) {
    switch (ev) {
      case XMLParser.ELEMENT_START: {
        const tag = p.eventName;
        const frame = { tag, attrs: {} };
        stack.push(frame);

        const lvl = headingLevel(tag);

        if(lvl) {
          md += '#'.repeat(lvl) + ' ';
        } else if(tag === 'pre') {
          md += '```\n';
        } else if(tag === 'b' || tag === 'strong') {
          ensureSpaceBefore();
          md += '**';
        } else if(tag === 'i' || tag === 'em') {
          ensureSpaceBefore();
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
          ensureSpaceBefore();
          md += '[';
        } else if(tag === 'img') {
          ensureSpaceBefore();
        }
        break;
      }

      case XMLParser.ATTRIBUTE: {
        const name = p.eventName;

        if(name === 'src' || name === 'href') {
          const frame = top();
          if(frame) frame.attrs[name] = p.eventValue;
        }
        break;
      }

      case XMLParser.TEXT: {
        const frame = top();
        const text = p.eventValue;

        if(frame && frame.tag === 'pre') {
          md += text; /* preserve whitespace inside <pre> */
        } else {
          const normalized = text
            .replace(/\s+/g, ' ')
            .replace(/[\u2018\u2019]/g, "'")
            .replace(/[\u201C\u201D\u00AB\u00BB]/g, '"');
          md += normalized;
        }
        break;
      }

      case XMLParser.ELEMENT_END: {
        const frame = stack.pop();
        if(!frame) break;

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
          md += '** ';
        } else if(tag === 'i' || tag === 'em') {
          md += '* ';
        } else if(tag === 'li') {
          md += '\n';
        } else if(tag === 'ul' || tag === 'ol') {
          md += '\n';
        } else if(tag === 'a') {
          md += '](' + (frame.attrs.href || '') + ') ';
        } else if(tag === 'img') {
          md += '![](' + (frame.attrs.src || '') + ')\n\n';
        }
        break;
      }
    }
  }

  return md.replace(/\n{3,}/g, '\n\n').trim() + '\n';
}

const isUrl = arg => /^https?:\/\//i.test(arg);

function load(arg) {
  const html = isUrl(arg) ? std.urlGet(arg) : std.loadFile(arg);

  if(html == null) throw new Error(`cannot ${isUrl(arg) ? 'fetch' : 'read'} '${arg}'`);

  return html;
}

function usage() {
  std.err.puts(`Usage: ${scriptArgs[0]} <file|URL>...\n\nConvert HTML files or http(s) URLs to Markdown on stdout.\n`);
}

function main(...args) {
  if(args.length == 0 || args.includes('-h') || args.includes('--help')) {
    usage();
    return args.length == 0 ? 1 : 0;
  }

  const out = [];
  let status = 0;

  for(const arg of args) {
    try {
      out.push(html2md(load(arg)));
    } catch(e) {
      std.err.puts(`${scriptArgs[0]}: ${arg}: ${e.message}\n`);
      status = 1;
    }
  }

  std.out.puts(out.join('\n'));
  return status;
}

if(isMainModule(import.meta.url)) std.exit(main(...scriptArgs.slice(1)));
