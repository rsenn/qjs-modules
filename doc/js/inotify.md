# inotify

Source: `lib/inotify.js` (pure JS) — default export: `inotify`

Linux `inotify` filesystem-watch bindings plus a class wrapper.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `inotify_init(flags=0)` | 0–1 | function | Creates an inotify instance; returns its fd. |
| `inotify_add_watch(fd, pathname, mask=IN_ALL_EVENTS)` | 2–3 | function | Adds/updates a watch; returns a watch descriptor. |
| `inotify_rm_watch(fd, wd)` | 2 | function | Removes a watch. |
| `inotify_close(fd)` | 1 | function | Closes an inotify fd. |
| `inotify` | — | class | Object-oriented wrapper around the above. **(default export)** |

Also re-exports the `IN_*` event-mask constants (e.g. `IN_CREATE`,
`IN_MODIFY`, `IN_DELETE`, `IN_ALL_EVENTS`).
