# Vendored Nuklear

- `nuklear.h`: Nuklear v4.13.4, public domain, from
  https://github.com/Immediate-Mode-UI/Nuklear
- `nuklear_sdl_renderer.h`: SDL2 Renderer backend from the same repository
  (`demo/sdl_renderer`), public domain.

The headers are vendored so the experiment builds without a system Nuklear
package. They are compiled by `paint-host.c` with the SDL2 renderer only.
