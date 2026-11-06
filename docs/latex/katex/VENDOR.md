# Vendored KaTeX

The `/latex/` page renders the LaTeX that Glon generates with this local copy
of KaTeX. It is served from the same site; there is no CDN dependency.

- **Package:** `katex` **0.16.22** (MIT; see `LICENSE`, kept verbatim).
  Copyright 2013-2020 Khan Academy and other contributors.
- **Source:** <https://registry.npmjs.org/katex/-/katex-0.16.22.tgz>
- **Tarball integrity**, as published by the npm registry and checked on
  download:
  `sha512-XCHRdUw4lf3SKBaJe4EvgqIuWwkPSo9XoeO8GjQW94Bp7TWv9hNhzZjZ+OH9yf1UmLygb7DIT5GSFQiyt16zYg==`

The files are copied unmodified from the tarball:

| Here | From the tarball |
|---|---|
| `LICENSE` | `package/LICENSE` |
| `katex.min.js` | `package/dist/katex.min.js` |
| `katex.min.css` | `package/dist/katex.min.css` |
| `fonts/*.woff2` (20 files) | `package/dist/fonts/*.woff2` |

**Subset.** Only the WOFF2 fonts are vendored. `katex.min.css` lists WOFF2
first in every `@font-face`, so every browser with WOFF2 support (all current
ones) uses them and never requests the omitted `.woff`/`.ttf` fallbacks.

To update: download the new tarball, check its integrity against
`https://registry.npmjs.org/katex/<version>` (`dist.integrity`), copy the same
files, and update the version and hash here and in `latex.html`.
