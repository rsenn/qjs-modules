# url

Source: `lib/url.js` (pure JS)

WHATWG `URL` / `URLSearchParams` implementation.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `URL` | class | Parsed URL with `href`, `protocol`, `host`, `hostname`, `port`, `pathname`, `search`, `searchParams`, `hash`, etc. |
| `URLSearchParams` | class | Query-string map: `get`, `getAll`, `set`, `append`, `delete`, `has`, `entries`, `toString`, iteration. |

## fileURLToPath / pathToFileURL

`fileURLToPath(url)` (string or `URL`; `TypeError` with `code` `ERR_INVALID_URL_SCHEME`,
`ERR_INVALID_FILE_URL_HOST`, `ERR_INVALID_FILE_URL_PATH`) and `pathToFileURL(path)`
(resolved against the cwd, percent-encoded as Node does). The legacy `url.parse()`,
`format()`, `resolve()` are not provided.
