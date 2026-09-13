# tools/site — the qjs-modules GitHub Pages site

Generates <https://rsenn.github.io/qjs-modules/> from the repo's own markdown.
No toolchain beyond `qjsm` itself: the markdown renderer and the syntax
highlighter are in here.

```sh
qjsm tools/site/build.js          # -> _site/
qjsm tools/site/build.js /tmp/out # or anywhere else
```

| File | Role |
|------|------|
| `build.js` | site map, link rewriting, page shell, search-index emission, entry point |
| `markdown.js` | CommonMark/GFM subset renderer, plus per-member anchors (see its header) |
| `highlight.js` | js / sh / c tokenizer for fenced blocks |
| `search.js` | client-side search box behaviour (loaded on every page) |
| `landing.html` | hand-written landing page body; `<x-code lang="…">` blocks go through `highlight.js` |
| `style.css` | one stylesheet, light and dark |
| `favicon.svg` | tab icon |

`highlight.js` and `favicon.svg` are shared verbatim with the sister projects
[qjs-lws](https://github.com/rsenn/qjs-lws) and
[qjs-opencv](https://github.com/rsenn/qjs-opencv)'s `tools/site`.
`markdown.js` and `style.css` started from those but have since diverged: this
project's `doc/{js,native}/*.md` document every method/property/constant as a
table row rather than a heading, so `markdown.js` adds per-row anchors (see
its file header), and `style.css`/`search.js` add the search box those don't
have. Fix a renderer bug common to all three before porting anything site-
specific back.

## Search

`build.js` collects every page title, `##`/`###` heading and anchored table
row into one flat array, written to `assets/search-index.js` as
`window.SEARCH_INDEX = [...]`. It's loaded as a plain `<script src>` (not
fetched) so the site still works from a local `file://` checkout, where
`fetch()` of a same-origin JSON file is blocked by CORS. `search.js` filters
that array client-side on each keystroke; press `/` anywhere on a page to
jump to the search box.

## Adding or moving a doc page

`NAV` at the top of `build.js` is the whole site map: each entry is
`[markdown source, output path, sidebar label]`. Adding a `doc/**.md` file
without listing it there means it does not get built, and inter-doc links
pointing at it fall back to a github.com blob URL instead of a site page.
