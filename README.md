# chip8run

**Browser-playable version with sample games and demos:** <https://tung.github.io/chip8run/>

**chip8run** is an interpreter (or "emulator") for CHIP-8: a bytecode format for the creation of video games for home computers made in the 1970s and 1980s like the COSMAC VIP.
CHIP-8 programs drive a 64-by-32 pixel monochrome display, while taking input from a 4-by-4 hexadecimal keyboard.
chip8run can run programs in the CHIP-8 format, often found with the `.ch8` suffix.

## Controls

Modern CHIP-8 programs usually use `WASD` for directions, `E` to confirm and `Q` to cancel.
These are also mapped to the cursor keys, spacebar/enter and backspace for convenience.

If those don't work, the full set of keys recognized by CHIP-8 is mapped like this:

```
+---------------+ +---------------+
| COSMAC VIP    | | What to press |
+---+---+---+---+ +---+---+---+---+
| 1 | 2 | 3 | C | | 1 | 2 | 3 | 4 |
+---+---+---+---+ +---+---+---+---+
| 4 | 5 | 6 | D | | Q | W | E | R |
+---+---+---+---| +---+---+---+---+
| 7 | 8 | 9 | E | | A | S | D | F |
+---+---+---+---+ +---+---+---+---+
| A | 0 | B | F | | Z | X | C | V |
+---+---+---+---+ +---+---+---+---+
```

You can press `=` (equals) and `-` (minus) to speed up or slow down emulation between 1x to 8x.

CHIP-8 program files (usually ending with `.ch8`) can be dragged and dropped into the chip8run window to run them.

## Building

chip8run can be compiled into a native binary on Linux.

1. Install development libraries for GL, X11, Xi and Xcursor.
2. Download `sokol-shdc` from <https://github.com/floooh/sokol-tools-bin>.
3. Type `make MODE=release SHDC=/path/to/sokol-shdc`.

The `chip8run` binary should appear in the `build/chip8run-release/out/` directory.

To compile the web version of chip8run, install [Emscripten](https://emscripten.org/), get `sokol-shdc` as above, then type the following:

```
make MODE=web-release SHDC=/path/to/sokol-shdc EMSDK_SH=/path/to/emsdk.sh
```

The `chip8run.html`, `chip8run.js` and `chip8run.wasm` files should appear in the `build/chip8run-web-release/out/` directory.

chip8run is written in C and uses [Sokol](https://github.com/floooh/sokol), so compiling native executables for Windows and macOS should only need minor changes.

## Licenses

chip8run is available under the [MIT License](/LICENSE.txt).

Sokol is available under the zlib/libpng license; see the header comment in `sokol/sokol_app.h`.

The CHIP-8 program files hosted with the site are from the [CHIP-8 Archive](https://johnearnest.github.io/chip8Archive/) under the Creative Commons Zero "No Rights Reserved" license.
