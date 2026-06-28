# Software Design Document — RadarApp (Polar + Rectangular Live View)

## 1. Overview
RadarApp is a Qt5/OpenGL desktop application that visualizes radar-style sensor data in two synchronized, side-by-side views:
- **Polar widget** (`PolarPyWidget`) — a sector ("fan") display showing intensity vs. range and angle.
- **Rectangular widget** (`RasterStripWidget`) — a scrolling 2D raster/heatmap showing the same underlying samples as rows (sweeps) vs. columns (range bins).

Both widgets subscribe to a shared data source (`SyntheticDataSource` / `MainController`) and update live as new sweeps arrive.

## 2. Goals
- Provide an intuitive radar-style live view for streamed sensor data.
- Allow operators to annotate the polar display with markers for regions of interest.
- Keep visual clutter low regardless of underlying data resolution.
- Support zero-config synthetic data generation for testing/demo purposes.

## 3. Architecture

```
MainWindow
 ├── SyntheticDataSource   (timer-driven data generator)
 ├── MainController        (data routing / normalization)
 ├── PolarPyWidget         (QOpenGLWidget — polar/fan rendering)
 └── RasterStripWidget     (QOpenGLWidget — rectangular raster rendering)
```

### 3.1 Data flow
1. `SyntheticDataSource::generate()` produces one row of samples per timer tick (default: a single moving "hot" cell — a unit impulse pattern: `100,0,0,...,0` → `0,100,0,...,0` → `0,0,100,...,0`, used to validate range/column mapping end-to-end).
2. `MainController::setInputData(...)` receives the row and forwards it to both widgets.
3. `PolarPyWidget::plotData(data, radialBins, angularBins)` stores the buffer and marks the GL VBOs dirty for rebuild.
4. `RasterStripWidget` appends the row to its scrolling buffer and triggers a repaint.

### 3.2 Rendering pipeline (Polar widget)
- **GL pass** (`paintGL()`): clears the buffer, rebuilds VBOs if dirty, then draws:
  - `drawSectors()` — the colorized data cells (OpenGL `GL_TRIANGLE_STRIP`, shader-based).
  - `drawGrid()` — the reference ring/spoke grid (OpenGL `GL_LINES`), **fixed at 5 rings / 8 spokes** regardless of actual data bin count, to keep the grid visually sparse and readable even at high angular resolutions.
  - `drawOverlay()` — `QPainter`-based overlay: angle tick labels, radial range labels, markers, crosshair, and hover tooltip.
- **Known limitation**: the `QPainter`-based overlay is currently invoked from within `paintGL()`. Under sustained high-frequency repaints (e.g., during continuous "Feed Data" streaming), this composition pattern can intermittently fail to render the overlay layer (a known Qt/OpenGL interaction quirk). This is a tracked issue — see Section 7.

### 3.3 Rendering pipeline (Rectangular widget)
- Renders into an offscreen `QImage` first (`QPainter ip(&img)`), then composites that image onto the widget (`QPainter p(this); p.drawImage(...)`), followed by a border draw (`p.drawRect(...)`). This two-stage approach avoids the QPainter-on-QOpenGLWidget text-dropping issue seen in the polar widget — **this is the proven pattern that should be ported to `PolarPyWidget` to resolve the overlay-visibility limitation noted above.**

## 4. Polar Widget — Key Features

### 4.1 Sector orientation
- Configurable angular span via `setStartAngle()` / `setEndAngle()`.
- Default span rotated so 0° bearing points to the top of the widget (`startAngle = -135°`, `endAngle = -45°`), rather than the mathematical default (0° = right/east).

### 4.2 Labels
- **Angular labels** (`paintExternalSpokeLabels`): drawn just outside the arc, styled in light blue (`QColor(200,200,255,200)`), font `Sans 8`.
- **Radial range labels**: reduced to 4 fixed reference values (25%, 50%, 75%, 100% of configured range), matched in font/color to the angular labels, positioned outside the arc edge (not inside the grid), with screen-space clamping to avoid clipping at widget edges.

### 4.3 Markers
Two marker types, added via right-click context menu:
- **Marker 1 (Triangle)** — a fixed-size triangular pin at the clicked (r, θ).
- **Marker 2 (Mini Sector)** — a curved-bottom wedge (built from an explicit inner-radius and outer-radius arc, not a pointed cone), with user-configurable **height** (radial depth multiplier) and **width** (angular span multiplier) prompted via `QInputDialog` at creation time.

Markers are stored in `m_markers` (`std::vector<PolarMarker>`) and persist independently of incoming data updates.

### 4.4 Grid density decoupling
The reference grid (rings + spokes) is intentionally decoupled from the actual data resolution (`m_radialBins` / `m_angularBins`). Both the GL-rendered grid (`buildGridVBO`) and the QPainter-rendered overlay grid use fixed counts (5 rings, 8 spokes) so the grid does not become visually cluttered as data resolution increases (e.g., when `m_angularBins` jumps from a default to 64 once live feeding starts).

## 5. Rectangular Widget — Key Features
- Scrolling raster display, one row per incoming sweep, one column per range bin.
- Y-axis labels read bottom-up (0 at the bottom, increasing upward) to match conventional graph orientation, while the underlying buffer still scrolls/renders with the newest row visually at the top.
- Light border drawn around the plot area for visual separation from the surrounding widget background.
- Hover tooltip shows the calibrated value (`m_minRange` to `m_maxRange` range) for the hovered cell; axis tick labels themselves remain raw sample/row indices, not physical units, since no unit conversion is defined in the current data path.

## 6. Configuration Constants (MainWindow.cpp / MainWindow.h)
| Constant | Value | Purpose |
|---|---|---|
| `ANGULAR_SAMPLE_COUNT` | 64 | Number of angular bins for both polar sector data and the synthetic pulse sweep. |
| `LOGICAL_WIDTH` | matches column count | Logical X-axis scale used for rectangular widget axis labels. |
| `DATA_MIN` / `DATA_MAX` | 0.0 / 100.0 | Default intensity range bounds. |

## 7. Known Issues / Follow-up Work
1. **Overlay visibility during live feed (Polar widget)** — `QPainter`-based overlay (labels, markers, crosshair) drawn from within `paintGL()` can intermittently fail to render under sustained high-frequency repaint, while rendering correctly once streaming stops. **Recommended fix**: port the offscreen-`QImage` compositing pattern already used in `RasterStripWidget::paintEvent` to `PolarPyWidget`, separating the GL render pass from the QPainter overlay pass via a dedicated `paintEvent` override.
2. **Drag-select sector region** — current marker placement is single-click (point + dialog-entered dimensions). A requested enhancement is a click-and-drag interaction to directly select a bounded (r, θ) region (e.g., 50–75 range by -11°–0°) and render it as a marker, rather than entering dimensions numerically.
3. **Units for range values** — `m_minRange` / `m_maxRange` are unitless floats in the current codebase; the physical unit (cm, m, etc.) is defined only by convention at the call site and is not labeled in the UI.

## 8. Build & Run
See `README.md` for build instructions (CMake + Qt5 + OpenGL, C++17).
