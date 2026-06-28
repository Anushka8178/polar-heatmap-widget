#include "rasterstripwidget.h"
#include <QMenu>
#include <QAction>
#include <QPolygonF>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <QVector2D>

RasterStripWidget::RasterStripWidget(QWidget* parent)
    : QOpenGLWidget(parent), m_colorMap(ColorMapType::Green)
{
    setMouseTracking(true);
}

RasterStripWidget::~RasterStripWidget()
{
    makeCurrent();
    m_vbo.destroy();
    m_vao.destroy();
    delete m_program;
    doneCurrent();
}

void RasterStripWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program = new QOpenGLShaderProgram;
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
        "attribute vec2 aPos;\n"
        "attribute vec4 aColor;\n"
        "varying vec4 vColor;\n"
        "uniform vec2 uViewportSize;\n"
        "void main() {\n"
        "    vec2 ndc = (aPos / uViewportSize) * 2.0 - 1.0;\n"
        "    ndc.y = -ndc.y;\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "}\n");
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
        "varying vec4 vColor;\n"
        "void main() { gl_FragColor = vColor; }\n");
    m_program->link();
    m_vao.create();
    m_vbo.create();
}

void RasterStripWidget::resizeGL(int w, int h)
{
    m_vpW = std::max(1, w);
    m_vpH = std::max(1, h);
    glViewport(0, 0, m_vpW, m_vpH);
}

void RasterStripWidget::setDataDimensions(int dataWidth, int dataHeight)
{
    m_dataWidth  = std::max(0, dataWidth);
    m_dataHeight = std::max(0, dataHeight);
    m_buffer.assign(size_t(m_dataWidth) * size_t(m_dataHeight), 0);
    update();
}

void RasterStripWidget::plotData(const unsigned char* rows, int rowCount, int cols,
                                  ScrollMode mode, int targetRow)
{
    if (mode == ScrollMode::PushFromTop)
    {
        const int n = std::min(rowCount, m_dataHeight);
        const unsigned char* src = rows + size_t(rowCount-n)*size_t(cols);
        const int keep = m_dataHeight - n;
        if (keep > 0)
            std::memmove(m_buffer.data() + size_t(n)*size_t(m_dataWidth),
                         m_buffer.data(), size_t(keep)*size_t(m_dataWidth));
        std::memcpy(m_buffer.data(), src, size_t(n)*size_t(m_dataWidth));
    }
    else if (mode == ScrollMode::PushFromBottom)
    {
        const int n = std::min(rowCount, m_dataHeight);
        const int keep = m_dataHeight - n;
        if (keep > 0)
            std::memmove(m_buffer.data(),
                         m_buffer.data() + size_t(n)*size_t(m_dataWidth),
                         size_t(keep)*size_t(m_dataWidth));
        std::memcpy(m_buffer.data() + size_t(keep)*size_t(m_dataWidth),
                    rows, size_t(n)*size_t(m_dataWidth));
    }
    else // Direct
    {
        int srcRow = 0, dstRow = targetRow, n = rowCount;
        if (dstRow < 0) { srcRow += -dstRow; n -= -dstRow; dstRow = 0; }
        if (dstRow + n > m_dataHeight) n = m_dataHeight - dstRow;
        if (n > 0)
            std::memcpy(m_buffer.data() + size_t(dstRow)*size_t(m_dataWidth),
                        rows + size_t(srcRow)*size_t(cols),
                        size_t(n)*size_t(m_dataWidth));
    }
    update();
}

void RasterStripWidget::plotData(const float* rows, int rowCount, int cols,
                                  ScrollMode mode, int targetRow)
{
    const int n = rowCount * cols;
    std::vector<unsigned char> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        buf[i] = static_cast<unsigned char>(
            std::max(0.0f, std::min(1.0f, rows[i])) * 255.0f + 0.5f);
    plotData(buf.data(), rowCount, cols, mode, targetRow);
}

void RasterStripWidget::setColorMap(ColorMapType type) { m_colorMap = ColorMap(type); update(); }
void RasterStripWidget::setColorMap(const ColorMap& map) { m_colorMap = map; update(); }

void RasterStripWidget::resetView()
{
    m_zoom = 1.0f;
    m_panOffset = QPointF(0,0);
    if (m_dataWidth > 0 && m_dataHeight > 0)
        m_buffer.assign(size_t(m_dataWidth)*size_t(m_dataHeight), 0);
    m_hoverValid = false;
    m_hoveredRow = -1;
    m_hoveredCol = -1;
    emit tooltipUpdated(false,-1,-1,0.0f);
    update();
}

void RasterStripWidget::computeGridRect(float& x0, float& y0, float& cellW, float& cellH) const
{
    const float margin = 0.12f;
    const float fw = float(m_vpW) * (1.0f - 2.0f*margin);
    const float fh = float(m_vpH) * (1.0f - 2.0f*margin);

    if (m_dataWidth <= 0 || m_dataHeight <= 0)
    {
        x0 = float(m_vpW)*margin; y0 = float(m_vpH)*margin;
        cellW = 0.0f; cellH = 0.0f;
        return;
    }

    cellW = fw / float(m_dataWidth);
    cellH = fh / float(m_dataHeight);
    x0 = (float(m_vpW) - cellW*float(m_dataWidth))  * 0.5f;
    y0 = (float(m_vpH) - cellH*float(m_dataHeight)) * 0.5f;
}

void RasterStripWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_dataWidth <= 0 || m_dataHeight <= 0 || m_buffer.empty()) return;

    float x0, y0, cellW, cellH;
    computeGridRect(x0, y0, cellW, cellH);

    std::vector<float> verts;
    verts.reserve(size_t(m_dataWidth)*size_t(m_dataHeight)*6*6);

    const float cx = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;
    auto applyZoomPan = [&](float px, float py, float& ox, float& oy) {
        ox = (px-cx)*m_zoom + cx + float(m_panOffset.x());
        oy = (py-cy)*m_zoom + cy + float(m_panOffset.y());
    };

    for (int row = 0; row < m_dataHeight; ++row)
    {
        for (int col = 0; col < m_dataWidth; ++col)
        {
            const unsigned char raw = m_buffer[size_t(row)*size_t(m_dataWidth)+size_t(col)];
            const QColor c = m_colorMap.sampleValue(int(raw), 255);
            const float r = float(c.redF()), g = float(c.greenF()),
                        b = float(c.blueF()), a = float(c.alphaF());
            const float left = x0+float(col)*cellW, top = y0+float(row)*cellH;
            float lx,ty,rxT,tyT,lxB,byB,rx2,by2;
            applyZoomPan(left,        top,    lx,  ty);
            applyZoomPan(left+cellW,  top,    rxT, tyT);
            applyZoomPan(left,        top+cellH, lxB, byB);
            applyZoomPan(left+cellW,  top+cellH, rx2, by2);
            verts.insert(verts.end(), {lx,ty,r,g,b,a});
            verts.insert(verts.end(), {rxT,tyT,r,g,b,a});
            verts.insert(verts.end(), {lxB,byB,r,g,b,a});
            verts.insert(verts.end(), {rxT,tyT,r,g,b,a});
            verts.insert(verts.end(), {rx2,by2,r,g,b,a});
            verts.insert(verts.end(), {lxB,byB,r,g,b,a});
        }
    }

    m_program->bind();
    m_program->setUniformValue("uViewportSize", QVector2D(float(m_vpW), float(m_vpH)));
    m_vao.bind(); m_vbo.bind();
    m_vbo.allocate(verts.data(), int(verts.size()*sizeof(float)));
    const int stride = 6*sizeof(float);
    m_program->enableAttributeArray("aPos");
    m_program->setAttributeBuffer("aPos", GL_FLOAT, 0, 2, stride);
    m_program->enableAttributeArray("aColor");
    m_program->setAttributeBuffer("aColor", GL_FLOAT, 2*sizeof(float), 4, stride);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()/6));
    m_vbo.release(); m_vao.release(); m_program->release();
}

// KEY FIX: render overlay into QImage first, then drawImage.
// This bypasses the Qt5 QOpenGLWidget embedded-mode text rendering bug
// where QPainter text is silently dropped when the widget is inside a layout.
void RasterStripWidget::paintEvent(QPaintEvent* event)
{
    QOpenGLWidget::paintEvent(event);

    QImage img(m_vpW, m_vpH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter ip(&img);
    ip.setRenderHint(QPainter::TextAntialiasing);
    drawOverlay(ip);
    ip.end();

    QPainter p(this);
    p.drawImage(0, 0, img);
    p.setPen(QPen(QColor(255,255,255,255), 2));
    p.drawRect(rect().adjusted(0,0,-1,-1));
}

void RasterStripWidget::drawOverlay() { QPainter p(this); drawOverlay(p); }

// Req 5: proper axis ticks — tick marks drawn as short lines, then labels beside them
void RasterStripWidget::drawOverlay(QPainter& p)
{
    float x0, y0, cellW, cellH;
    computeGridRect(x0, y0, cellW, cellH);
    if (cellW <= 0.0f || cellH <= 0.0f) return;

    const float gridW = cellW * float(m_dataWidth);
    const float gridH = cellH * float(m_dataHeight);
    const float cx    = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;

    auto applyZoomPan = [&](float px, float py, float& ox, float& oy) {
        ox = (px-cx)*m_zoom + cx + float(m_panOffset.x());
        oy = (py-cy)*m_zoom + cy + float(m_panOffset.y());
    };

    QFont f("Sans", 8);

    p.setFont(f);
    const QFontMetrics fm(f);
    const float th = float(fm.height());
    static const float fracs[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const float tickLen = 5.0f;
        // ── Reference grid lines (matches polar widget grid style) ────────────

    // ── Reference grid lines (matches polar widget grid style) ────────────
    p.setPen(QPen(QColor(180,180,180,90), 1.0));
    for (float frac : fracs)
        {
            float sx1, sy1, sx2, sy2;
            applyZoomPan(x0 + frac*gridW, y0, sx1, sy1);
            applyZoomPan(x0 + frac*gridW, y0 + gridH, sx2, sy2);
            p.drawLine(QPointF(sx1,sy1), QPointF(sx2,sy2));
        }
    for (float frac : fracs)
        {
            float sx1, sy1, sx2, sy2;
            applyZoomPan(x0, y0 + frac*gridH, sx1, sy1);
            applyZoomPan(x0 + gridW, y0 + frac*gridH, sx2, sy2);
            p.drawLine(QPointF(sx1,sy1), QPointF(sx2,sy2));
    }
    // ── X-axis ticks + labels (bottom of grid) ────────────────────────────
    p.setPen(QPen(QColor(200,200,255,200), 1.5f));
    for (float frac : fracs)
    {
        float sx, sy;
        applyZoomPan(x0 + frac*gridW, y0 + gridH, sx, sy);

        // tick mark
        p.drawLine(QPointF(sx, sy), QPointF(sx, sy + tickLen));

        // label
        const QString lbl = QString::number(int(std::round(frac * m_logicalWidth)));
        const float tw = float(fm.horizontalAdvance(lbl));
        const float lx = std::max(1.0f, std::min(sx - tw*0.5f, float(m_vpW)-tw-1.0f));
        const float ly = std::min(sy + tickLen + th, float(m_vpH)-2.0f);
        p.setPen(QColor(200,200,255,200));
        p.drawText(QPointF(lx, ly), lbl);
        p.setPen(QPen(QColor(200,200,255,200), 1.5f));
    }

    // ── Y-axis ticks + labels (left of grid) ──────────────────────────────
    p.setPen(QPen(QColor(200,200,255,200), 1.5f));
    for (float frac : fracs)
    {
        float sx, sy;
        applyZoomPan(x0, y0 + frac*gridH, sx, sy);

        // tick mark
        p.drawLine(QPointF(sx, sy), QPointF(sx - tickLen, sy));

        // label
        const QString lbl = QString::number(int(std::round((1.0f-frac) * m_logicalHeight)));
        const float tw = float(fm.horizontalAdvance(lbl));
        const float lx = std::max(1.0f, sx - tickLen - tw - 2.0f);
        const float ly = std::max(th, std::min(sy + th*0.3f, float(m_vpH)-2.0f));
        p.setPen(QColor(200,200,255,200));
        p.drawText(QPointF(lx, ly), lbl);
        p.setPen(QPen(QColor(200,200,255,200), 1.5f));
    }

    // ── Crosshair + hover labels ───────────────────────────────────────────
    if (m_hoverValid && m_hoveredRow >= 0 && m_hoveredCol >= 0)
    {
        const float cgx = x0 + (float(m_hoveredCol)+0.5f)*cellW;
        const float cgy = y0 + (float(m_hoveredRow)+0.5f)*cellH;
        float hx1,hy1,hx2,hy2,vx1,vy1,vx2,vy2,scx,scy;
        applyZoomPan(x0,        cgy, hx1,hy1);
        applyZoomPan(x0+gridW,  cgy, hx2,hy2);
        applyZoomPan(cgx, y0,        vx1,vy1);
        applyZoomPan(cgx, y0+gridH,  vx2,vy2);
        applyZoomPan(cgx, cgy,       scx,scy);

        QPen crossPen(QColor(0,220,255,100), 1.0, Qt::DotLine);
        p.setPen(crossPen);
        p.drawLine(QPointF(hx1,hy1), QPointF(hx2,hy2));
        p.drawLine(QPointF(vx1,vy1), QPointF(vx2,vy2));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255,255,255,220));
        p.drawEllipse(QPointF(scx,scy), 3.0, 3.0);

        QFont cf("Sans", 8);
        p.setFont(cf);
        const QFontMetrics cfm(cf);
        const float cth = float(cfm.height());

        // X hover label
        {
            const float colFrac = (m_dataWidth > 0)
                ? (float(m_hoveredCol)+0.5f)/float(m_dataWidth) : 0.0f;
            const QString lbl = QString::number(int(std::round(colFrac*m_logicalWidth)));
            const float tw = float(cfm.horizontalAdvance(lbl));
            float dummy, ly;
            applyZoomPan(cgx, y0+gridH+6.0f, dummy, ly);
            const float lx = std::max(1.0f, std::min(scx-tw*0.5f, float(m_vpW)-tw-1.0f));
            ly = std::min(ly+cth, float(m_vpH)-2.0f);
            p.setPen(QColor(0,220,255,240));
            p.setBrush(QColor(60,60,60,200));
            p.drawRect(QRectF(lx-3, ly-cth, tw+6.0f, cth+4.0f));
            p.drawText(QPointF(lx,ly), lbl);
        }
        // Y hover label
        {
            const float rowFrac = (m_dataHeight > 0)
                ? (float(m_hoveredRow)+0.5f)/float(m_dataHeight) : 0.0f;
            const QString lbl = QString::number(int(std::round(rowFrac*m_logicalHeight)));
            const float tw = float(cfm.horizontalAdvance(lbl));
            float glsx, dummy;
            applyZoomPan(x0, cgy, glsx, dummy);
            const float lx = std::max(1.0f, glsx-tw-4.0f);
            const float ly = std::min(scy+float(cfm.height())*0.3f, float(m_vpH)-2.0f);
            p.setPen(QColor(0,220,255,240));
            p.setBrush(QColor(60,60,60,200));
            p.drawRect(QRectF(lx-3, ly-cth, tw+6.0f, cth+4.0f));
            p.drawText(QPointF(lx,ly), lbl);
        }
    }

    drawMarkers(p);
}

bool RasterStripWidget::pixelToCell(const QPoint& px, int& row, int& col) const
{
    if (m_dataWidth <= 0 || m_dataHeight <= 0) return false;
    float x0,y0,cellW,cellH;
    computeGridRect(x0,y0,cellW,cellH);
    if (cellW <= 0.0f || cellH <= 0.0f) return false;
    const float cx = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;
    const float gx = (float(px.x())-cx-float(m_panOffset.x()))/m_zoom + cx;
    const float gy = (float(px.y())-cy-float(m_panOffset.y()))/m_zoom + cy;
    col = int((gx-x0)/cellW);
    row = int((gy-y0)/cellH);
    if (col < 0 || col >= m_dataWidth || row < 0 || row >= m_dataHeight) return false;
    return true;
}

void RasterStripWidget::contextMenuEvent(QContextMenuEvent* e)
{
    int row=-1, col=-1;
    bool hit = pixelToCell(e->pos(), row, col);
    QMenu menu(this);
    QAction* actMark1   = menu.addAction("Add Marker 1 (Triangle)");
    QAction* actMark2   = menu.addAction("Add Marker 2 (Mini Cell)");
    menu.addSeparator();
    QAction* actClear   = menu.addAction("Clear all markers");
    menu.addSeparator();
    QAction* actReset   = menu.addAction("Reset view");
    actMark1->setEnabled(hit); actMark2->setEnabled(hit);
    QAction* chosen = menu.exec(e->globalPos());
    if      (chosen == actReset) resetView();
    else if (chosen == actClear) { m_markers.clear(); update(); }
    else if ((chosen == actMark1 || chosen == actMark2) && hit)
    {
        RasterMarker mk;
        mk.type = (chosen == actMark1) ? RasterMarkerType::Triangle : RasterMarkerType::MiniCell;
        mk.row = row; mk.col = col; mk.miniScale = m_miniMarkerScale;
        m_markers.push_back(mk);
        update();
    }
}

void RasterStripWidget::drawMarkers(QPainter& p)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    float x0,y0,cellW,cellH;
    computeGridRect(x0,y0,cellW,cellH);
    const float cx=float(m_vpW)*0.5f, cy=float(m_vpH)*0.5f;
    auto applyZoomPan = [&](float px, float py, float& ox, float& oy) {
        ox=(px-cx)*m_zoom+cx+float(m_panOffset.x());
        oy=(py-cy)*m_zoom+cy+float(m_panOffset.y());
    };
    for (int i = 0; i < int(m_markers.size()); ++i)
    {
        const RasterMarker& mk = m_markers[i];
        const float gx = x0+(float(mk.col)+0.5f)*cellW;
        const float gy = y0+(float(mk.row)+0.5f)*cellH;
        float cx2,cy2;
        applyZoomPan(gx,gy,cx2,cy2);
        const QColor col = (mk.type==RasterMarkerType::Triangle)
                           ? QColor(255,180,30,230) : QColor(30,220,220,230);
        if (mk.type==RasterMarkerType::Triangle)
        {
            const float sz=10.0f*m_zoom, h=sz*0.866f;
            QPolygonF tri;
            tri << QPointF(cx2,cy2-h*0.667f) << QPointF(cx2-sz*0.5f,cy2+h*0.333f) << QPointF(cx2+sz*0.5f,cy2+h*0.333f);
            p.setPen(QPen(col.darker(150),1.2)); p.setBrush(col);
            p.drawPolygon(tri);
            p.setPen(Qt::white); p.setFont(QFont("Monospace",7,QFont::Bold));
            p.drawText(QPointF(cx2+sz*0.5f+2,cy2-h*0.667f), QString("M%1").arg(i+1));
        }
        else
        {
            const float hw=cellW*mk.miniScale*0.5f*m_zoom, hh=cellH*mk.miniScale*0.5f*m_zoom;
            p.setPen(QPen(col.darker(150),1.2)); p.setBrush(col);
            p.drawRect(QRectF(cx2-hw,cy2-hh,hw*2,hh*2));
            p.setPen(Qt::white); p.setFont(QFont("Monospace",7,QFont::Bold));
            p.drawText(QPointF(cx2+hw+3,cy2), QString("M%1").arg(i+1));
        }
    }
    p.restore();
}

void RasterStripWidget::emitTooltip()
{
    if (!m_hoverValid||m_hoveredRow<0||m_hoveredCol<0||m_buffer.empty())
    { emit tooltipUpdated(false,-1,-1,0.0f); return; }
    const int idx=m_hoveredRow*m_dataWidth+m_hoveredCol;
    if (idx<0||idx>=int(m_buffer.size()))
    { emit tooltipUpdated(false,-1,-1,0.0f); return; }
    const float value=m_minRange+float(m_buffer[idx])/255.0f*(m_maxRange-m_minRange);
    emit tooltipUpdated(true,m_hoveredRow,m_hoveredCol,value);
}

void RasterStripWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos=e->pos();
    if (m_dragging) { m_panOffset+=QPointF(pos-m_lastMousePos); m_lastMousePos=pos; update(); }
    int row=-1,col=-1;
    const bool inside=pixelToCell(pos,row,col);
    if (inside)
    {
        if (row!=m_hoveredRow||col!=m_hoveredCol||!m_hoverValid)
        {
            m_hoveredRow=row; m_hoveredCol=col; m_hoverValid=true;
            const int idx=row*m_dataWidth+col;
            if (idx>=0&&idx<int(m_buffer.size()))
                emit cellHovered(row,col,int(m_buffer[idx]));
            emitTooltip();
        }
    }
    else if (m_hoverValid)
    { m_hoverValid=false; m_hoveredRow=-1; m_hoveredCol=-1; emitTooltip(); }
    update();
}

void RasterStripWidget::mousePressEvent(QMouseEvent* e)
{ if (e->button()==Qt::LeftButton) { m_dragging=true; m_lastMousePos=e->pos(); } }

void RasterStripWidget::mouseReleaseEvent(QMouseEvent* e)
{ if (e->button()==Qt::LeftButton) m_dragging=false; }

void RasterStripWidget::wheelEvent(QWheelEvent* e)
{
    const float factor=(e->angleDelta().y()>0)?1.15f:1.0f/1.15f;
    m_zoom=std::max(0.2f,std::min(10.0f,m_zoom*factor));
    update();
}

void RasterStripWidget::leaveEvent(QEvent*)
{
    if (m_hoverValid)
    { m_hoverValid=false; m_hoveredRow=-1; m_hoveredCol=-1; emitTooltip(); update(); }
}
