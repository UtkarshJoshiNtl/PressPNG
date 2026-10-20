# TODO

## 1. Comparison slider instead of the viewport dropdown

Instead of switching the viewport between *source* and *compressed* via a
dropdown, show both at once with an interactive **wipe slider**:

- Drag a handle across the viewport: everything left of it shows the source,
  everything right shows the compressed result (classic before/after split
  view).
- Works in both *Fit* and *100%* zoom modes; handle position is a fraction of
  the displayed image, not the window.
- Requires rendering the source and the encoded result at the same scale into
  one composite, so artifacts from the chosen quality/target size are visible
  anywhere in the image, not just side by side.
- Bonus: show a small diff heatmap (per-pixel delta amplified) at the
  split-line position.

## 2. Explicit output dimensions + transforms (crop / resize / rotate …)

The solver currently only reduces quality and scales internally to meet the
byte target. Users sometimes need control over the output itself:

- **Choose dimensions**: explicit width × height (with aspect-ratio lock and
  an "original size" reset). If quality alone can't reach the target, drop to
  the requested resolution as well.
- **Optional transforms** applied before compression, with live preview:
  - crop (draggable rectangle, aspect presets: free / 1:1 / 4:3 / 16:9 …)
  - resize (user-specified pixels or percentage)
  - rotate (90° steps + free angle) and flip (horizontal / vertical)
- The chosen dimensions and transforms feed the existing solver; preview,
  estimate, and final output all reflect them.
- CLI flags mirroring the same options (`--crop`, `--rotate`, `--size`, …).

## 3. Port to Qt and Web

One engine, three front ends — `presspng_core` stays pure C++ and is reused
as-is:

- **Qt desktop**: replace the hand-rolled Win32 GUI with Qt 6 (Widgets or
  QML) so the app runs natively on Windows, Linux, and macOS from one
  codebase. Also unlocks the comparison slider and transform UI from TODO 1–2
  with proper widgets.
- **Web**: compile the core to WebAssembly with Emscripten and ship a browser
  UI (drag & drop, canvas-based comparison slider). All processing happens
  locally in the browser — images are never uploaded.
