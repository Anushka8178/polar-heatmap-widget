#pragma once

#include <QPaintEvent>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMatrix4x4>
#include <QPointF>
#include <QPoint>
#include <QString>
#include <QColor>
#include <string>
#include <vector>
#include <utility>

#include "colormap.h"

class QMouseEvent;
class QWheelEvent;
class QContextMenuEvent;

struct TooltipData
{
    bool valid = false;
    int ring = -1;
    int sector = -1;
    float value = 0.0f;
    float percentage = 0.0f;
    float minRange = 0.0f;
    float maxRange = 100.0f;
    int decimals = 1;

    QString ringId() const;
    QString sectorId() const;
    QString valueText() const;
    QString percentageText() const;
    QString summary() const;
};

enum class MarkerType { Triangle, MiniSector };

struct PolarMarker
{
    MarkerType type;
    float r     = 0.0f;
    float theta = 0.0f;
    float outerMult = 8.0f;
    float widthMult = 1.0f;
    QPointF clickedPixel;
    float miniScale = 0.01f;
};

class PolarPyWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    enum class CursorShape { Default, Crosshair, Triangle, Sector };

    explicit PolarPyWidget(QWidget* parent = nullptr);
    ~PolarPyWidget() override;

    void setCumulativeMode(bool on) { m_cumulativeMode = on; }
    void clearCumulativeData() { m_data.clear(); m_radialBins=0; m_angularBins=0; m_vboDirty=true; update(); }
    void setLogicalThetaScale(float scale);
    float logicalThetaScale() const;
    void setMiniMarkerScale(float s);
    void setLastUpdateHighlight(bool on) { m_highlightLastUpdate = on; update(); }

    void plotData(const float* data, int radialBins, int angularBins);
    void plotData(char* data, int radialBins, int angularBins);
    void plotDataRange(char* data, int ringFirst, int ringLast, int secFirst, int secLast);
    void initSweepBuffer(int radialBins, int angularBins);
    void setCurrentSweepRing(int ring) { m_currentSweepRing = ring; update(); }
    int  currentSweepRing() const { return m_currentSweepRing; }

    static QColor mapValueToColor(int value);
    static std::vector<unsigned char> generateTestData(int rows, int cols);

    void setMinRange(float value);
    void setMaxRange(float value);
    void setStartAngle(float deg);
    void setEndAngle(float deg);
    float minRange()    const { return m_minRange; }
    float maxRange()    const { return m_maxRange; }
    float startAngle()  const { return m_startAngle; }
    float endAngle()    const { return m_endAngle; }
    int   radialBins()  const { return m_radialBins; }
    int   angularBins() const { return m_angularBins; }
    void setColorMap(const ColorMap& map);
    void setColorMap(ColorMapType type);
    void setColorMapPreset(ColorMapType type) { setColorMap(type); }
    bool setCustomColorMap(const QString& jsonFilePath, std::string* errorOut = nullptr);
    const ColorMap& colorMap() const { return m_colorMap; }

    int selectedRing() const { return m_selectedRing; }
    int selectedSector() const { return m_selectedSector; }

    void resetView();
    void setCursorShape(CursorShape shape, int w = 16, int h = 16);

    TooltipData buildTooltipData() const;

signals:
    void sectorHovered(int ring, int sector, int rawValue);
    void positionHovered(float r, float theta);
    void sectorSelected(int ring, int sector, int rawValue);
    void tooltipUpdated(const TooltipData& tooltip);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void leaveEvent(QEvent* e) override;   // FIX: clears crosshair on mouse leave

private:
    bool initShaders();
    bool validate();
    void applyCustomCursor();
    void buildSectorVBO();
    void buildSectorVBORange(int ringFirst, int ringLast);
    void buildGridVBO();
    void drawSectors();
    void drawGrid();
    void drawOverlay();
    void drawRangeLabels();
    void drawArcScaleLabels();
    void drawExternalLabels();
    void drawCrosshair();
    void drawMarkers(QPainter& p);
    void emitTooltip();
    void computeDrawBounds(float& outScale, float& ox, float& oy) const;
    bool pixelToPolar(const QPoint& px, float& r, float& theta) const;
    bool polarToCell(float r, float theta, int& ring, int& sector) const;

    static constexpr int ARC_STEPS = 28;
    static constexpr int GRID_ARC  = 180;

    std::vector<unsigned char> m_data;
    int m_radialBins  = 0;
    int m_angularBins = 0;

    std::vector<std::pair<int,int>> m_pendingRanges;
    int m_currentSweepRing = -1;
    bool m_vboDirty = true;

    float m_minRange    = 0.0f;
    float m_maxRange    = 100.0f;
    float m_startAngle  = -45.0f;   // Req 4: default -45 to +45
    float m_endAngle    =  45.0f;

    ColorMap m_colorMap{ColorMapType::Green};  // Req 3: green only

    float   m_zoom      = 1.0f;
    QPointF m_panOffset = {0.0f, 0.0f};
    bool    m_dragging  = false;
    QPoint  m_lastMousePos;

    int m_vpW = 1;
    int m_vpH = 1;

    QOpenGLShaderProgram*    m_program    = nullptr;
    QOpenGLTexture*          m_bgTexture  = nullptr;
    QOpenGLBuffer            m_sectorVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_sectorVAO;
    QOpenGLBuffer            m_gridVBO{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_gridVAO;
    QMatrix4x4               m_projection;
    int                      m_uMVP = -1;
    int                      m_sectorVertexCount = 0;
    int                      m_gridRingVerts     = 0;
    int                      m_gridSpokeVerts    = 0;

    int m_selectedRing   = -1;
    int m_selectedSector = -1;
    int m_hoveredRing    = -1;
    int m_hoveredSector  = -1;
    QPoint m_hoverPixel;

    bool    m_crosshairVisible = false;
    QPointF m_crosshairPixel;
    float   m_crosshairR     = 0.0f;
    float   m_crosshairTheta = 0.0f;

    CursorShape m_cursorShape = CursorShape::Default;
    int         m_cursorW = 16;
    int         m_cursorH = 16;

    std::vector<PolarMarker> m_markers;
    float m_miniMarkerScale = 0.01f;
    int  m_lastAngularSector = -1;
    bool m_highlightLastUpdate = true;

    float m_logicalThetaScale = 1.0f;
    bool  m_cumulativeMode    = false;

    std::string m_errorMsg;
};
