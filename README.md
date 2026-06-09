# Polar Heatmap Widget — Day 3 (Guide Review Update)

**Student:** Anushka Das

---

## Changes Applied (Guide Feedback)

| # | Change | Status |
|---|--------|--------|
| 1 | Upgraded to OpenGL 3.3 Core Profile | ✓ Done |
| 2 | Range labels on concentric rings | ✓ Done |
| 3 | Replaced glBegin/glEnd with VBO + VAO | ✓ Done |
| 4 | Qt texture support (background texture) | ✓ Done |
| 5 | Proper destructor with GL resource cleanup | ✓ Done |
| 6 | Dynamic screen utilization for any angular span | ✓ Done |

---

## Build & Run

```bash
mkdir build && cd build
cmake ..
make -j4
./PolarHeatmapWidget
```

---

## Test dynamic layout

In `main.cpp`, change `setEndAngle(360)` to:
- `setEndAngle(180)` — half circle, fills top half
- `setEndAngle(90)`  — quarter circle, fills top-right
