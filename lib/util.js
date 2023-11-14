// ==UserScript==
// @name         GitHub
// @namespace    https://github.com/
// @version      0.1
// @description  try to take over the world!
// @author       You
// @match        https://github.com/
// @icon         https://www.google.com/s2/favicons?sz=64&domain=github.com
// @grant        none
// ==/UserScript==

function define(obj, ...args) {
  for(let props of args) {
    const desc = Object.getOwnPropertyDescriptors(props);

    for(let prop in desc) {
      const { enumerable, ...d } = desc[prop];

      try {
        delete obj[prop];
      } catch(e) {}

      Object.defineProperty(obj, prop, { ...d, enumerable: false });
    }
  }
  // obj = extend(obj, props, desc => (delete desc.configurable, delete desc.enumerable));

  return obj;
}
function memoize(fn, cache = new Map()) {
  return define(
    function Memoize(n, ...rest) {
      let r;
      if((r = cache.get(n))) return r;
      r = fn.call(this, n, ...rest);
      cache.set(n, r);
      return r;
    },
    { cache }
  );
}

window.getRepos = () =>
  [...document.querySelectorAll('#user-repositories-list > ul > li > div.col-10.col-lg-9.d-inline-block')].map(e => [
    e.querySelector('a').href,
    e.querySelector('h3').nextElementSibling?.querySelector('a')?.href
  ]);
window.parseXML = xmlStr => new DOMParser().parseFromString(xmlStr, 'text/html');
window.fetchURL = url =>
  fetch(url, {
    headers: {
      accept: 'text/html, application/xhtml+xml',
      'accept-language': 'en-US,en;q=0.9',
      'cache-control': 'no-cache',
      pragma: 'no-cache',
      'sec-ch-ua': '"Chromium";v="118", "Brave";v="118", "Not=A?Brand";v="99"',
      'sec-ch-ua-mobile': '?0',
      'sec-ch-ua-platform': '"Linux"',
      'sec-fetch-dest': 'empty',
      'sec-fetch-mode': 'cors',
      'sec-fetch-site': 'same-origin',
      'sec-gpc': '1',
      'turbo-frame': 'user-profile-frame'
    },
    referrer: window.location.href,
    referrerPolicy: 'strict-origin-when-cross-origin',
    method: 'GET',
    mode: 'cors',
    credentials: 'include'
  }).then(r => r.text());

window.getPage = (url = window.href) => {
  const { page } = Object.fromEntries([...new URL(url).searchParams]);
  return page ? +page : 1;
};

Object.defineProperty(window, 'href', {
  get: memoize(() => {
    const url = new URL(window.location.href);
    Object.defineProperty(url, 'query', {
      get: memoize(function () {
        return Object.fromEntries([...this.searchParams]);
      })
    });
    return url;
  })
});

window.makeQuery = obj => Object.entries(obj).reduce((s, [n, v], i) => s + (i == 0 ? '?' : '&') + n + '=' + v, '');
window.makeURL = (base = window.href, query = {}) => (base + '').replace(/\?.*/g, '') + makeQuery(query);
window.nextURL = (u = window.href) => makeURL(u, { ...u.query, page: getPage() + 1 });
window.fetchNext = (u = window.href) => fetchURL(nextURL(u));
