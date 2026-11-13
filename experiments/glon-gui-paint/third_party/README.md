# Vendored Nuklear

- `nuklear.h`: Nuklear v4.13.4, public domain, from
  https://github.com/Immediate-Mode-UI/Nuklear
- `nuklear_sdl_renderer.h`: SDL2 Renderer backend from the same repository
  (`demo/sdl_renderer`), public domain.

The headers are vendored so the experiment builds without a system Nuklear
package. They are compiled by `paint-host.c` with the SDL2 renderer only.

`nfd/` contains Native File Dialog Extended (Zlib license) sources:
`nfd.h`, `nfd_gtk.cpp`, `nfd_linux_shared.hpp` and `LICENSE`, from
https://github.com/btzy/nativefiledialog-extended. The Makefile builds it with
the GTK3 backend when `pkg-config gtk+-3.0` is available; otherwise the
application keeps the Nuklear path-entry fallback.

`libdecor-cairo/` contains the libdecor 0.1.0 Cairo decoration plugin under the
MIT license (see `libdecor-cairo/LICENSE`), from
<https://gitlab.freedesktop.org/libdecor/libdecor> tag `0.1.0`. The local patch
`0001-wslg-keep-shadow-surface-mapped.patch` keeps the border/shadow surface
mapped but transparent while maximized on WSLg, avoiding a stale ghost shadow.
See `libdecor-cairo/README.md` for the exact reason, ABI selection and build
requirements.
