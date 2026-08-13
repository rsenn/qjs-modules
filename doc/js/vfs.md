# vfs

Source: `lib/vfs.js` (pure JS)

Virtual filesystem layers presenting a uniform file API.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `UnionFS` | class | Overlays several filesystems into one namespace (first match wins). |
| `ArchiveFS` | class | Exposes an archive (via the [`archive`](archive.md) binding) as a readable filesystem. |
