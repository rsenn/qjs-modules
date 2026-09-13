/**
 * Client-side search box behaviour, shared by every generated page.
 *
 * window.SEARCH_INDEX (assets/search-index.js, loaded first) is one flat
 * array of { u: url, t: title, p: parent title, k: 'page'|'section'|'member' }
 * covering every page, heading and documented member across the site. `u` is
 * always site-root-relative (e.g. "docs/native/blob.html#arraybuffer"); this
 * script joins it with the current page's root prefix (body[data-root], the
 * same "../" chain build.js already uses for stylesheet/asset links) so the
 * same index works unmodified at any depth.
 *
 * Plain substring match, ranked by match position then title length - the
 * doc set is a few hundred entries, not enough to need fuzzy scoring.
 */
(function () {
  var input = document.getElementById('site-search');
  if (!input) return;

  var root = document.body.dataset.root || '';
  var items = window.SEARCH_INDEX || [];

  var panel = document.createElement('div');
  panel.className = 'search-results';
  panel.hidden = true;
  input.parentNode.appendChild(panel);

  var active = -1;

  var KIND_LABEL = { page: 'page', section: 'section', member: 'member' };

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function search(q) {
    q = q.trim().toLowerCase();
    if (!q) return [];
    var hits = [];
    for (var i = 0; i < items.length; i++) {
      var idx = items[i].t.toLowerCase().indexOf(q);
      if (idx >= 0) hits.push({ item: items[i], idx: idx });
    }
    hits.sort(function (a, b) {
      if (a.idx !== b.idx) return a.idx - b.idx;
      return a.item.t.length - b.item.t.length;
    });
    return hits.slice(0, 20).map(function (h) { return h.item; });
  }

  function render(list) {
    panel.innerHTML = '';
    active = -1;
    if (!list.length) { panel.hidden = true; return; }
    list.forEach(function (it) {
      var a = document.createElement('a');
      a.href = root + it.u;
      a.tabIndex = -1;
      a.innerHTML = '<span class="sr-title">' + esc(it.t) + '</span>' +
        '<span class="sr-meta">' + (it.p ? esc(it.p) + ' · ' : '') + KIND_LABEL[it.k] + '</span>';
      panel.appendChild(a);
    });
    panel.hidden = false;
  }

  function setActive(n) {
    var links = panel.querySelectorAll('a');
    active = Math.max(0, Math.min(n, links.length - 1));
    links.forEach(function (l, i) { l.classList.toggle('active', i === active); });
    if (links[active]) links[active].scrollIntoView({ block: 'nearest' });
  }

  input.addEventListener('input', function () { render(search(input.value)); });
  input.addEventListener('focus', function () { if (input.value) render(search(input.value)); });

  input.addEventListener('keydown', function (e) {
    var links = panel.querySelectorAll('a');
    if (e.key === 'ArrowDown') { e.preventDefault(); if (links.length) setActive(active + 1); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); if (links.length) setActive(active - 1); }
    else if (e.key === 'Enter') { if (active >= 0 && links[active]) location.href = links[active].href; }
    else if (e.key === 'Escape') { panel.hidden = true; input.blur(); }
  });

  document.addEventListener('click', function (e) {
    if (e.target !== input && !panel.contains(e.target)) panel.hidden = true;
  });

  // '/' focuses search from anywhere on the page, like GitHub's own search.
  document.addEventListener('keydown', function (e) {
    if (e.key !== '/' || e.target === input) return;
    var tag = (e.target && e.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || e.target.isContentEditable) return;
    e.preventDefault();
    input.focus();
  });
})();
