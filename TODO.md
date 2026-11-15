# TODO

## Done

### Comparison slider + diff heatmap

The Qt preview shows the source and the compressed result at once, split by a
draggable wipe line whose position is a fraction of the displayed image (not
the window), so it works in both *Fit* and manual zoom. A toggle overlays an
amplified per-pixel difference heatmap of the split region.

### Explicit output dimensions + transforms

- Output width × height with an aspect-ratio lock; the values are a per-axis
  ceiling and never upscale. Leaving them at the natural size preserves the
  original and the fast-copy path.
- Crop (draggable handles, free / 1:1 / 4:3 / 3:2 / 16:9 presets), rotate in
  90° steps, and horizontal/vertical flip, applied crop → rotate/flip →
  resize, all from the original pixels.
- `Compressor::estimate()` runs the same pipeline as `compress()` (no file
  written), so preview, numbers, and output always agree.
- CLI flags: `--resize WxH`, `--crop x,y,w,h`, `--rotate 90|180|270`,
  `--flip h|v|both`.

### Qt desktop front end

`presspng_core` stays pure C++ and is reused as-is; the Qt 6 Widgets GUI is
the primary front end (`PRESSPNG_QT_GUI`), with the hand-rolled Win32 GUI
kept as a fallback (`PRESSPNG_WIN32_GUI`, Windows only). The theme follows
the OS light/dark colour scheme, overridable via `PRESSPNG_THEME`.

## Next

### Web front end

Compile the core to WebAssembly with Emscripten and ship a browser UI
(drag & drop, canvas-based comparison slider). All processing happens locally
in the browser — images are never uploaded.

### Smaller follow-ups

- Batch queue (compress a folder / multiple selections at once).
- Free-angle rotation.
- Additional output formats (WebP) via one new `IImageCodec` subclass.
