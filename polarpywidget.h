#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QColor>
#include <QMatrix4x4>
#include <vector>
#include <string>

/**
 * PolarPyWidget — Day 3 (Guide Review Update)
 *
 * Changes applied per guide feedback:
 *   1. Upgraded to modern OpenGL 3.3 Core Profile
 *   2. Range labels drawn on concentric rings
 *   3. glBegin/glEnd replaced with VBOs + VAOs
 *   4. Qt texture support integrated (background texture)
 *   5. Proper destructor added for resource cleanup
 *   6. Dynamic screen utilization based on angular span
 *
 * Original Day 3 features retained:
 *   - plotData()        : accepts 2D data buffer
 *   - mapValueToColor() : heatmap colour mapping 0-255
 *   - Filled coloured sectors per data cell
 *   - Polar grid overlay (rings + spokes)
 *   - Input validation
 */
class PolarPyWidget : public QOpenGLWidget,
                      protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    // -------------------------------------------------------
    // Change 5: explicit destructor (guide requirement)
    // -------------------------------------------------------
    explicit PolarPyWidget(QWidget* parent = nullptr);
    ~PolarPyWidget();

    // --- Data input ---
    void plotData(char* data, int radialBins, int angularBins);

    // --- Colour mapping ---
    static QColor mapValueToColor(int value);

    // --- Range / angle config ---
    void setMinRange(float value);
    void setMaxRange(float value);
    void setStartAngle(float degrees);
    void setEndAngle(float degrees);

    float minRange()   const { return m_minRange; }
    float maxRange()   const { return m_maxRange; }
    float startAngle() const { return m_startAngle; }
    float endAngle()   const { return m_endAngle; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    // --- Rendering ---
    void buildSectorVBO();      // Change 3: build VBO for all sectors
    void buildGridVBO();        // Change 3: build VBO for grid lines
    void drawSectors();
    void drawGrid();
    void drawRangeLabels();     // Change 2: range label rendering

    // --- Shaders ---
    bool initShaders();

    // --- Compute draw bounds based on angular span ---
    // Change 6: dynamic screen utilization
    void computeDrawBounds(float& scaleOut,
                           float& offsetXOut,
                           float& offsetYOut) const;

    bool validate();

    // -------------------------------------------------------
    // Modern OpenGL objects (Change 1 + 3)
    // -------------------------------------------------------
    QOpenGLShaderProgram* m_program     = nullptr;

    // Sector geometry
    QOpenGLVertexArrayObject m_sectorVAO;
    QOpenGLBuffer            m_sectorVBO;
    int                      m_sectorVertexCount = 0;

    // Grid geometry
    QOpenGLVertexArrayObject m_gridVAO;
    QOpenGLBuffer            m_gridVBO;
    int                      m_gridVertexCount = 0;

    // Change 4: Qt texture for background
    QOpenGLTexture* m_bgTexture = nullptr;

    // Uniform locations
    int m_uMVP       = -1;
    int m_uUseColor  = -1;
    int m_uTexture   = -1;

    // MVP matrix
    QMatrix4x4 m_projection;

    // -------------------------------------------------------
    // Data
    // -------------------------------------------------------
    std::vector<unsigned char> m_data;
    int m_radialBins  = 0;
    int m_angularBins = 0;

    // Config
    float m_minRange   =   0.0f;
    float m_maxRange   = 100.0f;
    float m_startAngle =   0.0f;
    float m_endAngle   = 360.0f;

    // Viewport
    int m_width  = 1;
    int m_height = 1;

    bool m_vboDirty = true;   // rebuild VBOs when data changes

    std::string m_errorMsg;

    static constexpr int ARC_STEPS = 24;
};
