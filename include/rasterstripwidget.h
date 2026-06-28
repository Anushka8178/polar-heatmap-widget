#pragma once

#include <QOpenGLWidget>
#include <QContextMenuEvent>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMouseEvent>
#include <QPainter>
#include <vector>
#include <cstdint>

#include "colormap.h"

enum class ScrollMode { PushFromTop, PushFromBottom, Direct };
enum class RasterMarkerType { Triangle, MiniCell };

struct RasterMarker
{
    RasterMarkerType type;
    int row = 0;
    int col = 0;
    float miniScale = 0.5f;
};

class RasterStripWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit RasterStripWidget(QWidget* parent = nullptr);
    ~RasterStripWidget() override;

    void setDataDimensions(int dataWidth, int dataHeight);
    void clearMarkers() { m_markers.clear(); update(); }
    void setMiniMarkerScale(float s) { m_miniMarkerScale = std::max(0.01f, s); }

    void plotData(const float* rows, int rowCount, int cols,
                  ScrollMode mode = ScrollMode::PushFromTop, int targetRow = 0);
    void plotData(const unsigned char* rows, int rowCount, int cols,
                  ScrollMode mode = ScrollMode::PushFromTop, int targetRow = 0);

    void setMinRange(float v) { m_minRange = v; update(); }
    void setMaxRange(float v) { m_maxRange = v; update(); }
    void setLogicalWidth(float w)  { m_logicalWidth  = w; update(); }
    void setLogicalHeight(float h) { m_logicalHeight = h; update(); }

    void setColorMap(ColorMapType type);
    void setColorMap(const ColorMap& map);

    void resetView();

    QSize sizeHint() const override { return QSize(1000, 600); }
    QSize minimumSizeHint() const override { return QSize(400, 240); }

signals:
    void cellHovered(int row, int col, int value);
    void tooltipUpdated(bool valid, int row, int col, float value);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void drawOverlay(QPainter& p);
    void drawOverlay();   // legacy no-arg, calls drawOverlay(p)
    bool pixelToCell(const QPoint& px, int& row, int& col) const;
    void drawMarkers(QPainter& p);
    void computeGridRect(float& x0, float& y0, float& cellW, float& cellH) const;
    void emitTooltip();

    int m_dataWidth  = 0;
    int m_dataHeight = 0;
    std::vector<unsigned char> m_buffer;

    ColorMap m_colorMap{ColorMapType::Green};  // Req 3
    float m_minRange = 0.0f;
    float m_maxRange = 255.0f;
    float m_logicalWidth  = 1000.0f;
    float m_logicalHeight = 600.0f;

    int m_vpW = 1;
    int m_vpH = 1;

    float   m_zoom      = 1.0f;
    QPointF m_panOffset = QPointF(0,0);
    bool    m_dragging  = false;
    QPoint  m_lastMousePos;

    int  m_hoveredRow = -1;
    int  m_hoveredCol = -1;
    bool m_hoverValid = false;

    std::vector<RasterMarker> m_markers;
    float m_miniMarkerScale = 0.5f;

    QOpenGLShaderProgram*    m_program = nullptr;
    QOpenGLBuffer            m_vbo;
    QOpenGLVertexArrayObject m_vao;
};
