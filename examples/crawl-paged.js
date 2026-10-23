import { Queue } from 'queue';
import { urlGet } from 'std';
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
  //if(hold.length) yield [...hold];
}

function* xmlToText(...args) {
  const parser = new XMLParser(...args);
  for(const tok of parser) {
    switch (tok.type) {
      case TEXT:
        yield tok.value;
      default:
        break;
    }
  }
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

    const generator = deferUntilNextMatch(parser, isRepo, l => [...l.splice(l.begin(), l.findLast(isLink))]);

    const projects = [];

    for(const tokens of generator) {
      const startIndex = tokens.findIndex(isAttr('href', t => t.value.startsWith(url.pathname + '/')));

      if(startIndex == -1) continue;
      tokens.splice(0, startIndex);

      const obj = { href: tokens[0].value };

      const forkedIndex = tokens.findIndex(t => /^Forked/.test(t.value));

      if(forkedIndex != -1) obj.forked = tokens.slice(forkedIndex).find(isAttr('href')).value;

      const pIndex = tokens.findIndex(isTag('p'));

      if(pIndex != -1) obj.description = tokens.slice(pIndex).find(({ type }) => type == TEXT).value;

      projects.push(obj);

      console.log(obj);
      //  console.log(tokens.map(xmlToken));
    }

    if(projects.length == 0) break;
  }
}

main(...scriptArgs.slice(1));
