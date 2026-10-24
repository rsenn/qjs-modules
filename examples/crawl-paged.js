import { Queue } from 'queue';
import { urlGet } from 'std';
import { writeFileSync } from 'fs';
import { List } from 'list';
import { XMLParser, XMLPushParser, XMLTreeSerializer, XMLWriter } from 'xml';

const { ELEMENT_START, ATTRIBUTE, ELEMENT_END, TEXT, COMMENT, DECLARATION, INSTRUCTION, CDATA } = XMLParser;

/* for the base URL generate succession of URLs where paramName is 2,3,4,5,...
 * from the 2nd generated value on */
function* generatePagedUrls(baseUrl, paramName = 'page') {
  for(let i = 0; ; i++) {
    const url = new URL(baseUrl);

    if(i) url.searchParams.set(paramName, i + 1);
    else url.searchParams.delete(paramName);

    yield url.toString();
  }
}

function* fetchURLs(iterable) {
  for(const url of iterable) {
    console.log(`Fetching '${url}'...`);
    yield urlGet(url);
  }
}

/**
 * Creates a reader object that streams an iterable into a buffered queue.
 *
 * @param {Object} iterable - source iterable to pull items from.
 * @returns {Reader}          object with read() and close() method
 */
function readerFrom(iterable) {
  const q = new Queue();
  let it = (typeof iterable == 'string' ? [iterable] : iterable)[Symbol.iterator]();
  return {
    read(buf, len) {
      if(q.empty && it) {
        const { value, done } = it.next();
        if(value !== undefined) q.write(value);
        if(done) it = undefined;
      }
      return q.read(buf, 0, len);
    },
    close: () => it.return(),
  };
}

function* deferUntilNextMatch(source, predicate, transform = list => [...list]) {
  const hold = new List();

  for(const item of source) {
    hold.push(item);

    if(predicate(item)) {
      const result = transform(hold);
      if(result) yield result;
    }
  }

  if([...hold].some(predicate)) yield [...hold];
}

function xmlToken({ type, name, value }) {
  switch (type) {
    case ELEMENT_START:
      return `<${name}`;
    case ATTRIBUTE:
      return ` ${name}="${value}"`;
    case ELEMENT_END:
      return `</${name}`;
    case TEXT:
      return value;
    case COMMENT:
      return name;
  }
}

function main(baseUrl = 'https://github.com/rsenn?tab=repositories', paramName = 'page') {
  const it = fetchURLs(generatePagedUrls(baseUrl, 'page'));

  const url = new URL(baseUrl);

  const output = [];

  for(let response of it) {
    const rd = readerFrom(response);

    const parser = new XMLParser(rd, { tolerant: true, selfClosingTags: ['option'] });

    const isTag =
      tagName =>
      ({ type, name }) =>
        type == ELEMENT_START && name.toLowerCase() == tagName.toLowerCase();
    const isLink = isTag('a');
    const isAttr =
      (attrName, pred = () => true) =>
      tok =>
        tok.type == ATTRIBUTE && tok.name == attrName && pred(tok);
    const isRepo = token => isAttr('itemprop')(token) && /codeRepository/.test(token.value);
    const isText = token => token.type == TEXT;

    const generator = deferUntilNextMatch(parser, isRepo, l => [...l.splice(l.begin(), l.findLast(isLink))]);

    const projects = [];

    for(const tokens of generator) {
      /* find main repository href */
      const hrefAttr = tokens.find(isAttr('href', t => t.value.startsWith(url.pathname + '/')));
      if(!hrefAttr) continue;

      const obj = { href: hrefAttr.value };

      /* helper to slice tokens starting from the first match of a predicate */
      const fromMatch = pred => {
        const idx = tokens.findIndex(pred);
        return idx != -1 ? tokens.slice(idx) : [];
      };

      /* extract forked repository link */
      const forkedTokens = fromMatch(t => /^Forked/.test(t.value));
      const forkedHref = forkedTokens.find(isAttr('href'));
      if(forkedHref) obj.forked = forkedHref.value;

      /* extract description text from paragraph tag */
      const pTokens = fromMatch(isTag('p'));
      const descToken = pTokens.find(isText);
      if(descToken) obj.description = descToken.value.trim();

      //console.log(console.config({ compact: -2}), {pTokens})

      projects.push(obj);

      const { href, description, forked } = obj;

      console.log([href.replace(/.*\//g, ''), description.replace(/\.\s.*/g, ''), ...(forked ? [`[Forked from ${forked}]`] : [])].map((l, i, a) => l.padEnd(i == 0 ? 20 : 0)).join(' '));
    }

    if(projects.length == 0) break;

    output.push(...projects);
  }

  writeFileSync('projects.json', JSON.stringify(output, null, 2));
}

main(...scriptArgs.slice(1));
