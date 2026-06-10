#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QColor>
#include <QMatrix4x4>
#include <QPoint>
#include <vector>
#include <string>

/**
 * PolarPyWidget  —  Final Release
 *
 * Guide-review changes implemented:
 *   1. OpenGL 3.3 Core Profile  (QSurfaceFormat CoreProfile)
 *   2. Range labels on concentric rings
 *   3. glBegin/glEnd replaced with VBO + VAO
 *   4. Qt texture support  (QOpenGLTexture background)
 *   5. Proper destructor with full GL resource cleanup
 *   6. Dynamic screen utilization for any angular span
 *
 * Full feature set:
 *   - plotData()          : load 2D data buffer
 *   - mapValueToColor()   : Blue→Cyan→Green→Yellow→Red
 *   - Filled heatmap sectors (GL_TRIANGLE_STRIP)
 *   - Polar grid overlay  (rings + spokes, VBO)
 *   - Range labels (QPainter overlay)
 *   - Zoom (wheel), Pan (drag), Reset (double-click)
 *   - Hover tooltip  (ring / sector / value)
 *   - Sector selection with highlight
 *   - Qt signals: sectorSelected, sectorHovered
 *   - Dynamic bounding-box layout
 *   - Live animation support (call plotData() repeatedly)
 */
class PolarPyWidget : public QOpenGLWidget,
                      protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit PolarPyWidget(QWidget* parent = nullptr);
    ~PolarPyWidget();   // Change 5: proper destructor

    // ── Data ────────────────────────────────────────────
    void plotData(char* data, int radialBins, int angularBins);
    static QColor mapValueToColor(int value);
    static std::vector<unsigned char> generateTestData(int rows, int cols);

    // ── Configuration ───────────────────────────────────
    void setMinRange(float value);
    void setMaxRange(float value);
    void setStartAngle(float degrees);
    void setEndAngle(float degrees);

    float minRange()   const { return m_minRange; }
    float maxRange()   const { return m_maxRange; }
    float startAngle() const { return m_startAngle; }
    float endAngle()   const { return m_endAngle; }

    // ── Interaction state ────────────────────────────────
    int selectedRing()   const { return m_selectedRing; }
    int selectedSector() const { return m_selectedSector; }
    void resetView();

signals:
    void sectorSelected(int ring, int sector, int value);
    void sectorHovered (int ring, int sector, int value);

protected:
    // ── Qt OpenGL lifecycle ──────────────────────────────
    void initializeGL()              override;
    void resizeGL(int w, int h)      override;
    void paintGL()                   override;

    // ── Mouse events ─────────────────────────────────────
    void wheelEvent(QWheelEvent* e)        override;
    void mousePressEvent(QMouseEvent* e)   override;
    void mouseMoveEvent(QMouseEvent* e)    override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    // ── Shader setup ────────────────────────────────────
    bool initShaders();

    // ── VBO builders  (Change 3) ────────────────────────
    void buildSectorVBO();
    void buildGridVBO();

    // ── Draw calls ──────────────────────────────────────
    void drawSectors();
    void drawGrid();
    void drawRangeLabels();  // Change 2
    void drawOverlay();      // hover tooltip + status text

    // ── Coordinate helpers ───────────────────────────────
    bool pixelToPolar(const QPoint& px, float& r, float& theta) const;
    bool polarToCell (float r, float theta, int& ring, int& sector) const;
    void computeDrawBounds(float& scale, float& ox, float& oy) const; // Change 6

    // ── Validation ──────────────────────────────────────
    bool validate();

    // ── Modern GL objects  (Change 1 + 3 + 4) ───────────
    QOpenGLShaderProgram*    m_program   = nullptr;
    QOpenGLVertexArrayObject m_sectorVAO;
    QOpenGLBuffer            m_sectorVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_gridVAO;
    QOpenGLBuffer            m_gridVBO  {QOpenGLBuffer::VertexBuffer};
    QOpenGLTexture*          m_bgTexture = nullptr;  // Change 4

    int m_sectorVertexCount = 0;
    int m_gridRingVerts     = 0;
    int m_gridSpokeVerts    = 0;
    int m_uMVP              = -1;

    QMatrix4x4 m_projection;

    // ── Data ────────────────────────────────────────────
    std::vector<unsigned char> m_data;
    int m_radialBins  = 0;
    int m_angularBins = 0;

    // ── Config ──────────────────────────────────────────
    float m_minRange   =   0.0f;
    float m_maxRange   = 100.0f;
    float m_startAngle =   0.0f;
    float m_endAngle   = 360.0f;

    // ── View transform ──────────────────────────────────
    float   m_zoom      = 1.0f;
    QPointF m_panOffset = {0, 0};

    // ── Interaction ─────────────────────────────────────
    bool   m_dragging     = false;
    QPoint m_lastMousePos;
    int    m_hoveredRing   = -1;
    int    m_hoveredSector = -1;
    QPoint m_hoverPixel;
    int    m_selectedRing   = -1;
    int    m_selectedSector = -1;

    // ── State ───────────────────────────────────────────
    int         m_vpW = 1, m_vpH = 1;
    bool        m_vboDirty = true;
    std::string m_errorMsg;

    static constexpr int ARC_STEPS = 28;
    static constexpr int GRID_ARC  = 180;
};
