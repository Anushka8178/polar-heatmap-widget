# RadarApp — Polar + Rectangular Live View

A Qt5/OpenGL desktop application that visualizes radar-style sensor data in two synchronized views:
- **Polar widget** — a sector ("fan") display showing range vs. angle.
- **Rectangular (raster strip) widget** — a scrolling 2D heatmap showing the same data as rows (sweeps) vs. columns (range bins).

See `doc/SDD.md` for the full software design document.

## Build Requirements
- CMake ≥ 3.16
- Qt5 (Widgets, OpenGL components)
- OpenGL
- C++17 compiler

## Build
```bash
mkdir -p build && cd build
cmake ..
make
```

## Run
```bash
cd build
./RadarApp
```

## Usage
- **▶ Start** — starts the render/update pump (pushes buffered data to both widgets).
- **⚡ Feed Data** — starts the synthetic data generator (produces a moving impulse pattern, one row per tick). Both this and Start must be active together for live data to animate.
- **↺ Reset** — resets zoom/pan and clears accumulated history on both widgets.
- Right-click on the polar widget to add markers (Triangle or Mini Sector, the latter with configurable height/width).

## Project Structure
```
RadarApp/
├── CMakeLists.txt
├── main.cpp
├── MainWindow.h / MainWindow.cpp
├── doc/
│   └── SDD.md
├── include/
│   ├── colormap.h
│   ├── polarpywidget.h
│   └── rasterstripwidget.h
└── src/
    ├── colormap.cpp
    ├── polarpywidget.cpp
    └── rasterstripwidget.cpp
```
