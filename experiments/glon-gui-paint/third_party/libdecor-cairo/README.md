# libdecor cairo plugin (WSLg shadow fix)

This is the libdecor 0.1.0 Cairo decoration plugin, vendored because Glon Paint
needs one small WSLg-specific correction. Upstream source:

- <https://gitlab.freedesktop.org/libdecor/libdecor> tag `0.1.0`
- License: MIT (see `LICENSE`)

The plugin API version is 1 and this copy is ABI-compatible with libdecor 0.1.0
(the version shipped by Ubuntu 22.04, which WSLg uses). Glon Paint only selects
this directory when `libdecor-0.so.0` exports
`libdecor_configuration_get_window_size`, a 0.1.0 symbol absent from 0.2.x.

## Patch

When a window is maximized, libdecor hides its shadow/border subsurface by
attaching a null buffer. WSLg's compositor does not discard those old pixels, so
the previous shadow remains drawn inside the enlarged window (the "ghost
rectangle"). Upstream 0.2.x avoids a similar problem by keeping the border
surface mapped but transparent for tiled windows.

This copy applies the same idea to the 0.1.0 `DECORATION_TYPE_TITLE_ONLY` path
used for maximized/tiled windows:

- keep the border/shadow surface mapped and sized to the window;
- clear it to transparent instead of drawing the drop shadow;
- keep the title bar and the border input region, so drag, resize, maximize and
  restore continue to work.

Only the shadow pixels are omitted while maximized; the title bar, buttons and
resize borders remain active. The exact upstream change is in
`0001-wslg-keep-shadow-surface-mapped.patch`.

## Build dependencies

`make` compiles the plugin when `pkg-config` can find `libdecor-0`, `cairo`,
`pangocairo` and `wayland-cursor` (usually from `libdecor-0-dev`,
`libcairo2-dev`, `libpango1.0-dev`, `libwayland-dev`). The result is written to
`build/libdecor/libdecor-cairo.so`; Glon Paint points `LIBDECOR_PLUGIN_DIR` at
that directory when running on WSLg Wayland.
