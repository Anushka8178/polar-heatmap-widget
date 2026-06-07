# Polar Heatmap Widget — Day 2

**Project:** Polar Heatmap Widget using Qt and OpenGL  
**Student:** Anushka Das

---

## Day 2 — Work Completed

| # | Task | Status |
|---|------|--------|
| 1 | Created `PolarPyWidget` class | ✓ Done |
| 2 | Implemented polar coordinate calculations | ✓ Done |
| 3 | Drew concentric ring circles | ✓ Done |
| 4 | Drew radial spoke lines | ✓ Done |
| 5 | Configurable `setMinRange()` and `setMaxRange()` | ✓ Done |
| 6 | Configurable `setStartAngle()` and `setEndAngle()` | ✓ Done |
| 7 | Polar grid visualisation complete | ✓ Done |

---

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build config |
| `polarpywidget.h` | PolarPyWidget class declaration |
| `polarpywidget.cpp` | Polar grid rendering implementation |
| `main.cpp` | Application entry point |

---

## Build & Run

```bash
mkdir build && cd build
cmake ..
make -j4
./PolarHeatmapWidget
```

---

## Day 3 Plan
- Accept 2D data buffer via `plotData()`
- Implement heatmap colour mapping (value → colour)
- Render coloured sectors for each data cell
