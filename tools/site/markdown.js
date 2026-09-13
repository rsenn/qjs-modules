/**
 * Minimal CommonMark/GFM subset renderer, sized to what qjs-lws' own
 * markdown actually uses: ATX headings, fenced code, GFM tables, nested
 * ordered/unordered lists, thematic breaks, paragraphs, and inline
 * code/links/strong/em/autolinks. Deliberately does NOT implement
 * blockquotes, images, reference links, indented code blocks, raw HTML
 * or setext headings - none appear in doc/, and adding them would be
 * speculative. '_' is never an emphasis delimiter: the docs are full of
 * bare LWS_CALLBACK_* identifiers written outside code spans.
 *
 * Diverges from the qjs-lws/qjs-opencv original: doc/{js,native}/*.md document
 * every method/property/constant as the first column of a GFM table (headed
 * "Method", "Property", "Export", …), so tables whose header matches
 * RE_MEMBER_TABLE get a per-row id + anchor on that column, and render()
 * returns those as `members` alongside `headings` - one linkable, searchable
 * anchor per documented function/class member, not just per heading.
 *
 * Runs under qjsm; no dependencies.
 */

const RE_FENCE = /^(\s*)(```+|~~~+)\s*([A-Za-z0-9+#-]*)\s*$/;
const RE_HEADING = /^(#{1,6})\s+(.*?)\s*#*\s*$/;
const RE_HR = /^(?:\s*)(?:-{3,}|\*{3,}|_{3,})\s*$/;
const RE_ITEM = /^(\s*)([-*+]|\d{1,9}[.)])(\s+)(.*)$/;
const RE_TABLE_DELIM = /^\s*\|?\s*:?-{1,}:?\s*(\|\s*:?-{1,}:?\s*)*\|?\s*$/;

/* First-column headers that mark a table as documenting individually
 * linkable members (methods/properties/constants/…), rather than an
 * unrelated two-column table (e.g. a compatibility matrix). */
const RE_MEMBER_TABLE = /^(export|function|method|member|property|constant|class)s?( \/ function)?$/i;
/* A cell that is *only* a single code span - the shape every member table
 * uses for its first column - vs. prose that happens to contain code. */
const RE_MEMBER_CELL = /^`([^`]+)`$/;

/* Placeholder sentinel for inline slots - a codepoint markdown can't contain. */
const NUL = '\u0000';
const RE_SLOT = /\u0000(\d+)\u0000/g;

const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const escAttr = s => esc(s).replace(/"/g, '&quot;');

/** GitHub's heading-anchor slug: lowercase, drop punctuation, spaces -> '-'. */
export function slugify(text) {
  return text
    .replace(/<[^>]*>/g, '')
    .replace(/&(amp|lt|gt|quot);/g, '')
    .toLowerCase()
    .replace(/[^\w\s-]/g, '')
    .trim()
    .replace(/\s/g, '-'); // one space -> one hyphen, matching GitHub (never collapses runs)
}

/** Bare identifier out of a member-table cell's code span, for slugging. */
function memberName(code) {
  return code
    .replace(/\(.*\)$/, '')   // drop call args: 'find(root, predicate)' -> 'find'
    .replace(/^\[|\]$/g, '')  // unwrap computed names: '[Symbol.iterator]' -> 'Symbol.iterator'
    .replace(/\./g, '-');     // 'XMLParser.PARSE_AGAIN' -> 'XMLParser-PARSE_AGAIN'
}

/* ---------------------------------------------------------------- inline */

function inline(text, ctx, slots) {
  const owns = !slots;
  if (owns) slots = [];
  const put = html => NUL + (slots.push(html) - 1) + NUL;

  // 1. code spans, before anything else gets a chance to look inside them
  text = text.replace(/(`+)([^\n]*?)\1/g, (m, ticks, code) =>
    put('<code>' + esc(code.replace(/^ (.*) $/, '$1')) + '</code>'));

  // 2. everything still in play is literal text
  text = esc(text);

  // 3. links (labels can hold code slots, so recurse on the label)
  text = text.replace(/\[((?:[^\[\]]|\[[^\]]*\])*)\]\(\s*([^\s)]*)(?:\s+"([^"]*)")?\s*\)/g,
    (m, label, href, title) => {
      const url = ctx.link ? ctx.link(href) : href;
      const t = title ? ' title="' + escAttr(title) + '"' : '';
      const ext = /^[a-z][a-z0-9+.-]*:/i.test(url) ? ' target="_blank" rel="noopener"' : '';
      return put('<a href="' + escAttr(url) + '"' + t + ext + '>' + inline(label, ctx, slots) + '</a>');
    });

  // 4. bare URLs
  text = text.replace(/(^|[\s(])(https?:\/\/[^\s<>()]+[^\s<>().,;:])/g,
    (m, pre, url) => pre + put('<a href="' + escAttr(url) + '" target="_blank" rel="noopener">' + url + '</a>'));

  // 5. emphasis ('_' is intentionally not a delimiter - see file header)
  text = text.replace(/\*\*(?=\S)([\s\S]*?\S)\*\*/g, '<strong>$1</strong>');
  text = text.replace(/(^|[^*\w])\*(?=[^\s*])([^*]*?[^\s*])\*(?!\*)/g, '$1<em>$2</em>');

  // 6. restore slots (a link slot can itself contain code slots)
  if (owns)
    for (let i = 0; i < 8 && text.indexOf(NUL) >= 0; i++)
      text = text.replace(RE_SLOT, (m, n) => slots[+n]);

  return text;
}

/* ----------------------------------------------------------------- blocks */

/** Split a GFM table row, honouring \| escapes and pipes inside code spans. */
function splitRow(line) {
  const cells = [];
  let cur = '', tick = 0;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === '\\' && line[i + 1] === '|') { cur += '|'; i++; continue; }
    if (c === '`') tick ^= 1;
    if (c === '|' && !tick) { cells.push(cur); cur = ''; continue; }
    cur += c;
  }
  cells.push(cur);
  if (cells.length && !cells[0].trim()) cells.shift();
  if (cells.length && !cells[cells.length - 1].trim()) cells.pop();
  return cells.map(c => c.trim());
}

function alignments(delim) {
  return splitRow(delim).map(c =>
    c.startsWith(':') && c.endsWith(':') ? 'center'
      : c.endsWith(':') ? 'right'
      : c.startsWith(':') ? 'left' : '');
}

const indentOf = line => line.match(/^\s*/)[0].length;
const isBlank = line => !line.trim();

function blocks(lines, ctx) {
  const out = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];

    if (isBlank(line)) { i++; continue; }

    const fence = line.match(RE_FENCE);
    if (fence) {
      const pad = fence[1], marker = fence[2], lang = fence[3];
      const close = new RegExp('^\\s*' + marker[0] + '{' + marker.length + ',}\\s*$');
      const body = [];
      i++;
      while (i < lines.length && !close.test(lines[i])) {
        body.push(lines[i].startsWith(pad) ? lines[i].slice(pad.length) : lines[i].trimStart());
        i++;
      }
      i++; // closing fence
      const code = body.join('\n');
      const cls = lang ? ' class="language-' + escAttr(lang) + '"' : '';
      const html = ctx.highlight ? ctx.highlight(code, lang) : esc(code);
      out.push('<div class="codeblock"' + (lang ? ' data-lang="' + escAttr(lang) + '"' : '') +
        '><pre><code' + cls + '>' + html + '</code></pre></div>');
      continue;
    }

    const head = line.match(RE_HEADING);
    if (head) {
      const level = head[1].length;
      const id = ctx.slug(slugify(head[2]));
      ctx.headings.push({ level, id, text: head[2].replace(/[`*]/g, '') });
      if (level >= 2) ctx.section = { id, text: head[2].replace(/[`*]/g, '') };
      out.push('<h' + level + ' id="' + escAttr(id) + '">' +
        '<a class="anchor" href="#' + escAttr(id) + '" aria-hidden="true">#</a>' +
        inline(head[2], ctx) + '</h' + level + '>');
      i++;
      continue;
    }

    if (RE_HR.test(line) && !RE_ITEM.test(line)) { out.push('<hr>'); i++; continue; }

    if (line.indexOf('|') >= 0 && i + 1 < lines.length && RE_TABLE_DELIM.test(lines[i + 1])) {
      const align = alignments(lines[i + 1]);
      const headerCells = splitRow(line);
      const memberTable = RE_MEMBER_TABLE.test(headerCells[0] || '');
      const th = headerCells.map((c, n) =>
        '<th' + (align[n] ? ' style="text-align:' + align[n] + '"' : '') + '>' + inline(c, ctx) + '</th>').join('');
      i += 2;
      const rows = [];
      while (i < lines.length && !isBlank(lines[i]) && lines[i].indexOf('|') >= 0) {
        const cells = splitRow(lines[i]);
        const first = memberTable ? cells[0].match(RE_MEMBER_CELL) : null;
        rows.push('<tr>' + cells.map((c, n) => {
          const style = align[n] ? ' style="text-align:' + align[n] + '"' : '';
          if (n === 0 && first) {
            const text = first[1].replace(/[`*]/g, '');
            const id = ctx.slug((ctx.section ? ctx.section.id + '-' : '') + slugify(memberName(text)));
            ctx.members.push({ id, text, section: ctx.section && ctx.section.text });
            return '<td' + style + '><code id="' + escAttr(id) + '">' + esc(text) + '</code>' +
              '<a class="member-anchor" href="#' + escAttr(id) + '" aria-hidden="true">#</a></td>';
          }
          return '<td' + style + '>' + inline(c, ctx) + '</td>';
        }).join('') + '</tr>');
        i++;
      }
      out.push('<div class="tablewrap"><table><thead><tr>' + th + '</tr></thead><tbody>' +
        rows.join('') + '</tbody></table></div>');
      continue;
    }

    if (RE_ITEM.test(line)) { const r = list(lines, i, ctx); out.push(r.html); i = r.next; continue; }

    // paragraph: runs to the next blank line or the start of another block
    const para = [];
    while (i < lines.length && !isBlank(lines[i])) {
      const l = lines[i];
      if (para.length && (RE_FENCE.test(l) || RE_HEADING.test(l) || RE_ITEM.test(l) || RE_HR.test(l))) break;
      para.push(l.trim());
      i++;
    }
    out.push('<p>' + inline(para.join('\n'), ctx) + '</p>');
  }

  return out.join('\n');
}

function list(lines, start, ctx) {
  const first = lines[start].match(RE_ITEM);
  const base = indentOf(lines[start]);
  const ordered = /\d/.test(first[2]);
  const items = [];
  let i = start, loose = false, pendingBlank = false;

  while (i < lines.length) {
    if (isBlank(lines[i])) { pendingBlank = true; i++; continue; }

    const m = lines[i].match(RE_ITEM);
    const ind = indentOf(lines[i]);

    if (m && ind === base && /\d/.test(m[2]) === ordered) {
      if (pendingBlank && items.length) loose = true;
      pendingBlank = false;
      items.push({ indent: base + m[2].length + m[3].length, body: [m[4]] });
      i++;
      continue;
    }
    if (items.length && ind > base) {
      const cur = items[items.length - 1];
      if (pendingBlank) { cur.body.push(''); loose = true; pendingBlank = false; }
      cur.body.push(lines[i].slice(Math.min(ind, cur.indent)));
      i++;
      continue;
    }
    break;
  }

  const html = items.map(it => {
    const body = blocks(it.body, ctx);
    // a tight list drops the <p> wrapper around the item's leading paragraph
    const text = loose ? body : body.replace(/^<p>([\s\S]*?)<\/p>/, '$1');
    return '<li>' + text + '</li>';
  }).join('\n');

  const n = ordered ? +first[2].replace(/\D/g, '') : 0;
  const tag = ordered ? 'ol' : 'ul';
  return {
    html: '<' + tag + (n > 1 ? ' start="' + n + '"' : '') + '>\n' + html + '\n</' + tag + '>',
    next: i,
  };
}

/* ------------------------------------------------------------------- api */

/**
 * @param {string} src markdown source
 * @param {object} [opts]
 * @param {(href: string) => string} [opts.link] rewrite link targets
 * @param {(code: string, lang: string) => string} [opts.highlight] emit code HTML
 * @returns {{ html: string, headings: { level: number, id: string, text: string }[],
 *             members: { id: string, text: string, section: string|undefined }[] }}
 */
export function render(src, opts = {}) {
  const seen = new Map();
  const ctx = {
    link: opts.link,
    highlight: opts.highlight,
    headings: [],
    members: [],
    section: null,
    slug(base) {
      const id = base || 'section';
      const n = seen.get(id) || 0;
      seen.set(id, n + 1);
      return n ? id + '-' + n : id;
    },
  };
  const lines = src.replace(/\r\n?/g, '\n').replace(/\t/g, '    ').split('\n');
  return { html: blocks(lines, ctx), headings: ctx.headings, members: ctx.members };
}
