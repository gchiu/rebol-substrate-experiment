#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
src_path = root / "view/glon/widget_gallery.glon"
out_path = root / "view/glon/widget_gallery_full.glon"
src = src_path.read_text()
placeholder = 'gallery/full-source: ""'
if placeholder not in src:
    raise SystemExit("gallery/full-source placeholder not found")
chunks = [src[i:i + 400] for i in range(0, len(src), 400)] or [""]
parts = []
for chunk in chunks:
    values = " ".join(str(b) for b in chunk.encode())
    parts.append("mk-string [ " + values + " ]")
expr = "gallery/full-source: gallery/cat reduce [ " + " ".join(parts) + " ] 0"
out_path.write_text(src.replace(placeholder, expr, 1))
print(f"wrote {out_path} ({len(chunks)} chunks, {len(src)} source bytes)")
