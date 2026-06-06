# Polar Heatmap Widget — Day 1

**Project:** Polar Heatmap Widget using Qt and OpenGL  
**Student:** Anushka Das

---

## Day 1 — Work Completed

| # | Task | Status |
|---|------|--------|
| 1 | Development environment setup (Qt + OpenGL) | ✓ Done |
| 2 | CMake build configuration | ✓ Done |
| 3 | OpenGL widget creation (`MyGLWidget`) | ✓ Done |
| 4 | Rendering context initialization (`initializeGL`) | ✓ Done |
| 5 | Viewport management (`resizeGL`) | ✓ Done |
| 6 | OpenGL rendering verification | ✓ Done |
| 7 | Initial graphical output generated | ✓ Done |
| 8 | Polar heatmap architecture planning | ✓ Done |
| 9 | Technical documentation prepared | ✓ Done |

---

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build config — links Qt Widgets, Qt OpenGL, system OpenGL |
| `myglwidget.h` | Base OpenGL widget class declaration |
| `myglwidget.cpp` | `initializeGL` / `resizeGL` / `paintGL` implementation |
| `main.cpp` | Application entry point |

---

## Build & Run

```bash
mkdir build && cd build
cmake ..
make -j4
./PolarHeatmapWidget
```

**Requirements:** Qt 5.15+, CMake 3.16+, OpenGL driver.

---

## Day 2 Plan

- Create `PolarPyWidget` class
- Implement polar coordinate calculations (`x = r·cos θ`, `y = r·sin θ`)
- Draw concentric ring circles at each radial boundary
- Draw radial spoke lines at each angular division
- Support configurable `setMinRange()` and `setMaxRange()`
