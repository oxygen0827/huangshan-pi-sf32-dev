# Lua Runtime Source

This directory contains the production sources of Lua 5.5 used by the
Huangshan Runtime. The sources were copied from the `georgik/lua` ESP-IDF
component bundled with the local `vibeboard-runtime-gpl` reference project.

- Component: `georgik/lua` `5.5.0~7`
- Upstream: <https://www.lua.org/>
- License: Lua license, included in `LICENSE.txt`
- Local adaptation: 32-bit integers/floats through `luaconf.h`

The standalone interpreter, compiler, test suite and dynamic C module loader
are not built. The complete language VM and standard pure-Lua libraries are
embedded through the Runtime adapter.
