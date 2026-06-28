#include "polarpywidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QCursor>
#include <QtMath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
static void paintArcScaleLabels(QPainter& p,
                                 float ox, float oy, float scale,
                                 float startAngle, float endAngle,
                                 float minRange, float maxRange,
                                 float zoom, QPointF panOffset,
                                 int vpW, int vpH);

static void paintExternalSpokeLabels(QPainter& p,
                                      float ox, float oy, float scale,
                                      float startAngle, float endAngle,
                                      int numSpokes,
                                      float zoom, QPointF panOffset,
                                      int vpW, int vpH);

static void paintCrosshair(QPainter& p,
                            float ox, float oy, float scale,
                            float crossR, float crossTheta,
                            QPointF cursorPx,
                            int hoveredRing, int hoveredSector,
                            float hoveredValue,
                            float minRange, float maxRange,
                            float zoom, QPointF panOffset,
                            int vpW, int vpH);

// ─────────────────────────────────────────────────────────────────────────────
// TooltipData
// ─────────────────────────────────────────────────────────────────────────────
QString TooltipData::ringId() const    { return QString("R%1").arg(ring,   2, 10, QLatin1Char('0')); }
QString TooltipData::sectorId() const  { return QString("S%1").arg(sector, 2, 10, QLatin1Char('0')); }
QString TooltipData::valueText() const { return QString::number(double(value), 'f', decimals); }
QString TooltipData::percentageText() const { return QString::number(double(percentage), 'f', 1) + "%"; }
QString TooltipData::summary() const
{
    if (!valid) return "No selection";
    return QString("%1 | %2 | Value: %3 (%4)")
        .arg(ringId()).arg(sectorId()).arg(valueText()).arg(percentageText());
}

// ─────────────────────────────────────────────────────────────────────────────
// GLSL
// ─────────────────────────────────────────────────────────────────────────────
static const char* VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
PolarPyWidget::PolarPyWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    setFormat(fmt);
    setMouseTracking(true);
}

PolarPyWidget::~PolarPyWidget()
{
    makeCurrent();
    if (m_sectorVBO.isCreated()) m_sectorVBO.destroy();
    if (m_gridVBO.isCreated())   m_gridVBO.destroy();
    if (m_sectorVAO.isCreated()) m_sectorVAO.destroy();
    if (m_gridVAO.isCreated())   m_gridVAO.destroy();
    delete m_program;
    delete m_bgTexture;
    doneCurrent();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void PolarPyWidget::plotData(char* data, int radialBins, int angularBins)
{
    if (m_cumulativeMode && !m_data.empty()
        && radialBins == m_radialBins && angularBins == m_angularBins)
    {
        auto* src = reinterpret_cast<unsigned char*>(data);
        for (int i = 0; i < radialBins * angularBins; ++i)
            m_data[i] = std::max(m_data[i], src[i]);
        m_vboDirty = true;
        update();
        return;
    }
    if (!data || radialBins <= 0 || angularBins <= 0) return;
    m_radialBins  = radialBins;
    m_angularBins = angularBins;
    const int total = radialBins * angularBins;
    m_data.resize(total);
    std::memcpy(m_data.data(), data, total);
    m_vboDirty = true;
    m_pendingRanges.clear();
    update();
}

void PolarPyWidget::plotData(const float* data, int radialBins, int angularBins)
{
    if (!data || radialBins <= 0 || angularBins <= 0) return;
    const int n = radialBins * angularBins;
    std::vector<char> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        buf[size_t(i)] = static_cast<char>(static_cast<unsigned char>(
            std::max(0.0f, std::min(1.0f, data[i])) * 255.0f + 0.5f));
    plotData(buf.data(), radialBins, angularBins);
}

void PolarPyWidget::plotDataRange(char* data, int ringFirst, int ringLast,
                                   int secFirst, int secLast)
{
    if (!data || m_radialBins <= 0 || m_angularBins <= 0 || m_data.empty()) return;
    if (ringFirst < 0 || ringLast >= m_radialBins || ringFirst > ringLast) return;
    if (secFirst  < 0 || secLast  >= m_angularBins || secFirst > secLast)  return;
    for (int ring = ringFirst; ring <= ringLast; ++ring)
        for (int sec = secFirst; sec <= secLast; ++sec)
            m_data[ring * m_angularBins + sec] = static_cast<unsigned char>(*data++);
    bool merged = false;
    for (auto& range : m_pendingRanges)
    {
        if (ringFirst <= range.second + 1 && ringLast >= range.first - 1)
        { range.first = std::min(range.first, ringFirst); range.second = std::max(range.second, ringLast); merged = true; break; }
    }
    if (!merged) m_pendingRanges.push_back({ringFirst, ringLast});
    update();
}

QColor PolarPyWidget::mapValueToColor(int value) { return ColorMap(ColorMapType::Default).sampleValue(value, 255); }

std::vector<unsigned char> PolarPyWidget::generateTestData(int rows, int cols)
{
    std::vector<unsigned char> buf(rows * cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            buf[r * cols + c] = static_cast<unsigned char>((r * cols + c) % 256);
    return buf;
}

void PolarPyWidget::setMinRange(float v) { m_minRange = v; m_vboDirty = true; update(); }
void PolarPyWidget::setMaxRange(float v) { m_maxRange = v; m_vboDirty = true; update(); }
void PolarPyWidget::setStartAngle(float d) { m_startAngle = d; m_vboDirty = true; update(); }
void PolarPyWidget::setEndAngle(float d)   { m_endAngle   = d; m_vboDirty = true; update(); }

void PolarPyWidget::setColorMap(ColorMapType type) { m_colorMap = ColorMap(type); m_vboDirty = true; update(); }
void PolarPyWidget::setColorMap(const ColorMap& map) { m_colorMap = map; m_vboDirty = true; update(); }

bool PolarPyWidget::setCustomColorMap(const QString& jsonFilePath, std::string* errorOut)
{
    ColorMap loaded;
    if (!ColorMap::loadFromJsonFile(jsonFilePath, loaded, errorOut)) return false;
    m_colorMap = std::move(loaded);
    m_vboDirty = true;
    update();
    return true;
}

void PolarPyWidget::resetView() { m_zoom = 1.0f; m_panOffset = {0,0}; update(); }

void PolarPyWidget::setLogicalThetaScale(float scale) { m_logicalThetaScale = scale; update(); }
float PolarPyWidget::logicalThetaScale() const { return m_logicalThetaScale; }
void PolarPyWidget::setMiniMarkerScale(float s) { m_miniMarkerScale = s; update(); }

void PolarPyWidget::setCursorShape(CursorShape shape, int w, int h)
{
    m_cursorShape = shape;
    m_cursorW = std::max(4, w);
    m_cursorH = std::max(4, h);
    applyCustomCursor();
}

void PolarPyWidget::applyCustomCursor()
{
    switch (m_cursorShape)
    {
    case CursorShape::Default:   setCursor(Qt::ArrowCursor);  break;
    case CursorShape::Crosshair: setCursor(Qt::CrossCursor);  break;
    case CursorShape::Triangle:
    {
        QPixmap pix(m_cursorW + 2, m_cursorH + 2);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(QColor(0, 200, 255, 220));
        QPolygonF tri;
        tri << QPointF(m_cursorW/2.0, 0) << QPointF(0, m_cursorH) << QPointF(m_cursorW, m_cursorH);
        p.drawPolygon(tri);
        setCursor(QCursor(pix, m_cursorW/2, 0));
        break;
    }
    case CursorShape::Sector:
    {
        const int sz = std::max(m_cursorW, m_cursorH);
        QPixmap pix(sz+2, sz+2);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(QColor(255, 180, 0, 220));
        p.drawPie(QRectF(1, 1, sz, sz), 60*16, 60*16);
        setCursor(QCursor(pix, sz*3/4, sz/4));
        break;
    }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GL
// ─────────────────────────────────────────────────────────────────────────────
void PolarPyWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!initShaders()) m_errorMsg = "Shader compilation failed.";
    m_sectorVAO.create(); m_sectorVBO.create();
    m_gridVAO.create();   m_gridVBO.create();
}

void PolarPyWidget::resizeGL(int w, int h)
{
    m_vpW = w; m_vpH = h;
    glViewport(0, 0, w, h);
    m_projection.setToIdentity();
    m_projection.ortho(0.0f, float(w), float(h), 0.0f, -1.0f, 1.0f);
}


void PolarPyWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (!validate()) return;
    if (m_vboDirty) { buildSectorVBO(); buildGridVBO(); m_vboDirty = false; m_pendingRanges.clear(); }
    else if (!m_pendingRanges.empty())
    { for (const auto& r : m_pendingRanges) buildSectorVBORange(r.first, r.second); m_pendingRanges.clear(); }
    drawSectors();
    drawGrid();
    drawOverlay();
}

bool PolarPyWidget::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   VERT_SRC)) return false;
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, FRAG_SRC)) return false;
    if (!m_program->link()) return false;
    m_uMVP = m_program->uniformLocation("uMVP");
    return true;
}

bool PolarPyWidget::validate() { return m_program && m_program->isLinked(); }

void PolarPyWidget::computeDrawBounds(float& outScale, float& ox, float& oy) const
{
    const float spanDeg = std::abs(m_endAngle - m_startAngle);
    const float margin  = 0.10f;
    const float fw      = float(m_vpW) * (1.0f - 2.0f * margin);
    const float fh      = float(m_vpH) * (1.0f - 2.0f * margin);

    if (spanDeg >= 340.0f)
    {
        outScale = std::min(fw, fh) * 0.5f;
        ox = float(m_vpW) * 0.5f;
        oy = float(m_vpH) * 0.5f;
        return;
    }

    std::vector<QPointF> pts;
    pts.push_back({0,0});
    auto addAngle = [&](float deg) {
        const float rad = qDegreesToRadians(deg);
        pts.push_back({std::cos(rad), std::sin(rad)});
    };
    addAngle(m_startAngle);
    addAngle(m_endAngle);
    const float lo = std::min(m_startAngle, m_endAngle);
    const float hi = std::max(m_startAngle, m_endAngle);
    for (float c : {0.0f, 90.0f, 180.0f, 270.0f, 360.0f})
        if (c >= lo && c <= hi) addAngle(c);

    float minX =  1e9f, maxX = -1e9f, minY =  1e9f, maxY = -1e9f;
    for (const auto& p : pts)
    {
        minX = std::min(minX, float(p.x())); maxX = std::max(maxX, float(p.x()));
        minY = std::min(minY, float(p.y())); maxY = std::max(maxY, float(p.y()));
    }
    const float wedgeW = std::max(maxX - minX, 1e-4f);
    const float wedgeH = std::max(maxY - minY, 1e-4f);
    const float scale  = std::min(fw / wedgeW, fh / wedgeH);
    ox = float(m_vpW) * 0.5f - (minX + maxX) * 0.5f * scale;
    oy = float(m_vpH) * 0.5f - (minY + maxY) * 0.5f * scale;
    outScale = scale;
}

void PolarPyWidget::buildSectorVBO()
{
    if (m_data.empty() || m_radialBins <= 0 || m_angularBins <= 0) return;
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    const int vertsPerSector = (ARC_STEPS + 1) * 2;
    std::vector<float> verts;
    verts.reserve(m_radialBins * m_angularBins * vertsPerSector * 6);

    const float spanDeg = m_endAngle - m_startAngle;
    const float dTheta  = spanDeg / float(m_angularBins);

    for (int ring = 0; ring < m_radialBins; ++ring)
    {
        const float rInner = float(ring)     / float(m_radialBins);
        const float rOuter = float(ring + 1) / float(m_radialBins);
        for (int sec = 0; sec < m_angularBins; ++sec)
        {
            const int   raw  = int(m_data[ring * m_angularBins + sec]);
            const QColor c   = m_colorMap.sampleValue(raw, 255);
            const float r    = float(c.redF());
            const float g    = float(c.greenF());
            const float b    = float(c.blueF());
            const float a    = float(c.alphaF());
            const float hl   = (ring == m_selectedRing && sec == m_selectedSector) ? 1.3f : 1.0f;
            const float tS   = m_startAngle + float(sec)   * dTheta;
            const float tE   = m_startAngle + float(sec+1) * dTheta;
            for (int step = 0; step <= ARC_STEPS; ++step)
            {
                const float t     = float(step) / float(ARC_STEPS);
                const float angle = qDegreesToRadians(tS + t * (tE - tS));
                const float cosA  = std::cos(angle), sinA = std::sin(angle);
                verts.push_back(ox + rInner * scale * cosA); verts.push_back(oy + rInner * scale * sinA);
                verts.push_back(std::min(1.0f,r*hl)); verts.push_back(std::min(1.0f,g*hl));
                verts.push_back(std::min(1.0f,b*hl)); verts.push_back(a);
                verts.push_back(ox + rOuter * scale * cosA); verts.push_back(oy + rOuter * scale * sinA);
                verts.push_back(std::min(1.0f,r*hl)); verts.push_back(std::min(1.0f,g*hl));
                verts.push_back(std::min(1.0f,b*hl)); verts.push_back(a);
            }
        }
    }

    m_sectorVertexCount = int(verts.size()) / 6;
    m_sectorVAO.bind(); m_sectorVBO.bind();
    m_sectorVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_sectorVBO.allocate(verts.data(), int(verts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), reinterpret_cast<void*>(2*sizeof(float)));
    m_sectorVBO.release(); m_sectorVAO.release();
}

void PolarPyWidget::buildSectorVBORange(int ringFirst, int ringLast)
{
    if (m_data.empty() || m_radialBins <= 0 || m_angularBins <= 0) return;
    if (!m_sectorVBO.isCreated()) return;
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    const int   vertsPerSector  = (ARC_STEPS + 1) * 2;
    const int   floatsPerSector = vertsPerSector * 6;
    const int   floatsPerRing   = floatsPerSector * m_angularBins;
    const float spanDeg         = m_endAngle - m_startAngle;
    const float dTheta          = spanDeg / float(m_angularBins);
    const int   ringCount       = ringLast - ringFirst + 1;

    std::vector<float> verts;
    verts.reserve(ringCount * floatsPerRing);

    for (int ring = ringFirst; ring <= ringLast; ++ring)
    {
        const float rInner = float(ring)     / float(m_radialBins);
        const float rOuter = float(ring + 1) / float(m_radialBins);
        for (int sec = 0; sec < m_angularBins; ++sec)
        {
            const int   raw = int(m_data[ring * m_angularBins + sec]);
            const QColor c  = m_colorMap.sampleValue(raw, 255);
            const float r   = float(c.redF()), g = float(c.greenF()), b = float(c.blueF()), a = float(c.alphaF());
            const float hl  = (ring == m_selectedRing && sec == m_selectedSector) ? 1.3f : 1.0f;
            const float tS  = m_startAngle + float(sec)   * dTheta;
            const float tE  = m_startAngle + float(sec+1) * dTheta;
            for (int step = 0; step <= ARC_STEPS; ++step)
            {
                const float t = float(step)/float(ARC_STEPS);
                const float angle = qDegreesToRadians(tS + t*(tE-tS));
                const float cosA = std::cos(angle), sinA = std::sin(angle);
                verts.push_back(ox+rInner*scale*cosA); verts.push_back(oy+rInner*scale*sinA);
                verts.push_back(std::min(1.0f,r*hl)); verts.push_back(std::min(1.0f,g*hl));
                verts.push_back(std::min(1.0f,b*hl)); verts.push_back(a);
                verts.push_back(ox+rOuter*scale*cosA); verts.push_back(oy+rOuter*scale*sinA);
                verts.push_back(std::min(1.0f,r*hl)); verts.push_back(std::min(1.0f,g*hl));
                verts.push_back(std::min(1.0f,b*hl)); verts.push_back(a);
            }
        }
    }

    const GLintptr   byteOffset = GLintptr(ringFirst)  * floatsPerRing * sizeof(float);
    const GLsizeiptr byteSize   = GLsizeiptr(verts.size()) * sizeof(float);
    m_sectorVAO.bind(); m_sectorVBO.bind();
    glBufferSubData(GL_ARRAY_BUFFER, byteOffset, byteSize, verts.data());
    m_sectorVBO.release(); m_sectorVAO.release();
}

void PolarPyWidget::buildGridVBO()
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);
    std::vector<float> ringVerts, spokeVerts;
    const int numRings = 5;
    for (int ring = 0; ring <= numRings; ++ring)
    {
        const float radius = scale * float(ring) / float(numRings);
        for (int step = 0; step <= GRID_ARC; ++step)
        {
            const float angle = qDegreesToRadians(m_startAngle + float(step)/float(GRID_ARC)*(m_endAngle-m_startAngle));
            ringVerts.push_back(ox + radius*std::cos(angle));
            ringVerts.push_back(oy + radius*std::sin(angle));
        }
    }
    m_gridRingVerts = int(ringVerts.size()) / 2;
    const int numSpokes = 8;
    for (int s = 0; s <= numSpokes; ++s)
    {
        const float angle = qDegreesToRadians(m_startAngle + float(s)/float(numSpokes)*(m_endAngle-m_startAngle));
        spokeVerts.push_back(ox); spokeVerts.push_back(oy);
        spokeVerts.push_back(ox + scale*std::cos(angle));
        spokeVerts.push_back(oy + scale*std::sin(angle));
    }
    m_gridSpokeVerts = int(spokeVerts.size()) / 2;
    std::vector<float> combined;
    combined.insert(combined.end(), ringVerts.begin(), ringVerts.end());
    combined.insert(combined.end(), spokeVerts.begin(), spokeVerts.end());
    m_gridVAO.bind(); m_gridVBO.bind();
    m_gridVBO.allocate(combined.data(), int(combined.size()*sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glDisableVertexAttribArray(1);
    m_gridVBO.release(); m_gridVAO.release();
}

void PolarPyWidget::drawSectors()
{
    if (m_sectorVertexCount == 0) return;
    QMatrix4x4 mvp = m_projection;
    float cx = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;
    mvp.translate(cx+m_panOffset.x(), cy+m_panOffset.y());
    mvp.scale(m_zoom);
    mvp.translate(-cx, -cy);
    m_program->bind();
    m_program->setUniformValue(m_uMVP, mvp);
    m_sectorVAO.bind();
    const int vps = (ARC_STEPS+1)*2;
    int offset = 0;
    for (int i = 0; i < m_radialBins * m_angularBins; ++i)
    { glDrawArrays(GL_TRIANGLE_STRIP, offset, vps); offset += vps; }
    m_sectorVAO.release();
    m_program->release();
}

void PolarPyWidget::drawGrid() {}
void PolarPyWidget::drawRangeLabels()   {}
void PolarPyWidget::drawArcScaleLabels(){}
void PolarPyWidget::drawExternalLabels(){}
void PolarPyWidget::drawCrosshair()     {}

void PolarPyWidget::drawOverlay()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    painter.save();
    const float cx = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;
    painter.translate(cx+m_panOffset.x(), cy+m_panOffset.y());
    painter.scale(double(m_zoom), double(m_zoom));
    painter.translate(-cx, -cy);

    const int   numRings  = 5;
    const int   numSpokes = 8;
    const float spanDeg   = m_endAngle - m_startAngle;

    painter.setPen(QPen(QColor(180,180,180,90), 1.0));
    for (int ring = 1; ring <= numRings; ++ring)
    {
        const float radius = scale * float(ring) / float(numRings);
        QPainterPath arc;
        bool first = true;
        for (int step = 0; step <= GRID_ARC; ++step)
        {
            const float angle = qDegreesToRadians(m_startAngle + float(step)/float(GRID_ARC)*spanDeg);
            const float x = ox + radius*std::cos(angle), y = oy + radius*std::sin(angle);
            if (first) { arc.moveTo(x,y); first=false; } else arc.lineTo(x,y);
        }
        painter.drawPath(arc);
    }

    for (int s = 0; s <= numSpokes; ++s)
    {
        const float angle = qDegreesToRadians(m_startAngle + float(s)/float(numSpokes)*spanDeg);
        painter.drawLine(QPointF(ox,oy), QPointF(ox+scale*std::cos(angle), oy+scale*std::sin(angle)));
    }

    painter.restore();
    QFont labelFont2("Sans", 8);
    painter.setFont(labelFont2);
    painter.setPen(QColor(200,200,255,200));
    static const float radialPcts[] = {25.0f, 50.0f, 75.0f, 100.0f};
    for (float pct : radialPcts)
    {
        const float radius = scale * (pct/100.0f) * 1.15f;
        const float valDeg = m_minRange + (m_maxRange-m_minRange)*(pct/100.0f);
        const float lAngle = qDegreesToRadians(m_startAngle - 6.0f);
        const float wx = ox + radius*std::cos(lAngle);
        const float wy = oy + radius*std::sin(lAngle);
        float lx = (wx-cx)*m_zoom + cx + m_panOffset.x();
        float ly = (wy-cy)*m_zoom + cy + m_panOffset.y();
        const QString lbl2 = QString::number(int(valDeg));
        const QFontMetrics fm2(labelFont2);
        const int tw2 = fm2.horizontalAdvance(lbl2);
        lx = std::max(float(tw2), std::min(lx, float(m_vpW - tw2 - 2)));
        ly = std::max(2.0f, std::min(ly, float(m_vpH - 2)));
        painter.drawText(QPointF(lx,ly), lbl2);
    }

    paintArcScaleLabels(painter, ox, oy, scale,
                        m_startAngle, m_endAngle, m_minRange, m_maxRange,
                        m_zoom, m_panOffset, m_vpW, m_vpH);
    paintExternalSpokeLabels(painter, ox, oy, scale,
                             m_startAngle, m_endAngle, 8,
                             m_zoom, m_panOffset, m_vpW, m_vpH);

    if (m_crosshairVisible)
    {
        float hoveredValue = 0.0f;
        if (m_hoveredRing >= 0 && m_hoveredSector >= 0 && !m_data.empty())
        {
            const int idx = m_hoveredRing * m_angularBins + m_hoveredSector;
            if (idx >= 0 && idx < int(m_data.size()))
                hoveredValue = m_minRange + (m_maxRange-m_minRange)*float(m_data[idx])/255.0f;
        }
        paintCrosshair(painter, ox, oy, scale,
                       m_crosshairR, m_crosshairTheta, m_crosshairPixel,
                       m_hoveredRing, m_hoveredSector, hoveredValue,
                       m_minRange, m_maxRange,
                       m_zoom, m_panOffset, m_vpW, m_vpH);
    }

    if (m_hoveredRing >= 0 && m_hoveredSector >= 0 && !m_data.empty())
    {
        const int idx = m_hoveredRing * m_angularBins + m_hoveredSector;
        if (idx >= 0 && idx < int(m_data.size()))
        {
            const float mapped = m_minRange + (m_maxRange-m_minRange)*float(m_data[idx])/255.0f;
            const QString tip  = QString("Ring %1 | Sec %2 | %3")
                .arg(m_hoveredRing).arg(m_hoveredSector)
                .arg(QString::number(double(mapped), 'f', 1));
            const QFont tipFont("Sans", 9);
            const QFontMetrics fm(tipFont);
            const QRect bounds = fm.boundingRect(tip);
            int tx = m_hoverPixel.x() + 14;
            int ty = m_hoverPixel.y() - 6;
            tx = std::min(tx, m_vpW - bounds.width() - 10);
            ty = std::max(ty, bounds.height() + 4);
            painter.setFont(tipFont);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(20,20,20,200));
            painter.drawRoundedRect(tx-4, ty-bounds.height()-2, bounds.width()+10, bounds.height()+6, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(tx, ty, tip);
        }
    }

    if (!m_errorMsg.empty())
    {
        painter.setPen(Qt::red);
        painter.setFont(QFont("Sans", 10));
        painter.drawText(10, 20, QString::fromStdString(m_errorMsg));
    }
    drawMarkers(painter);
}

TooltipData PolarPyWidget::buildTooltipData() const
{
    TooltipData td;
    if (m_hoveredRing < 0 || m_hoveredSector < 0 || m_data.empty()) return td;
    const int idx = m_hoveredRing * m_angularBins + m_hoveredSector;
    if (idx < 0 || idx >= int(m_data.size())) return td;
    const int raw = int(m_data[idx]);
    td.valid     = true;
    td.ring      = m_hoveredRing;
    td.sector    = m_hoveredSector;
    td.minRange  = m_minRange;
    td.maxRange  = m_maxRange;
    td.decimals  = 1;
    td.value     = m_minRange + float(raw)/255.0f*(m_maxRange-m_minRange);
    const float span = m_maxRange - m_minRange;
    td.percentage = (span > 1e-6f) ? (td.value-m_minRange)/span*100.0f : 0.0f;
    return td;
}

void PolarPyWidget::emitTooltip() { emit tooltipUpdated(buildTooltipData()); }

bool PolarPyWidget::pixelToPolar(const QPoint& px, float& r, float& theta) const
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);
    const float cx = float(m_vpW)*0.5f, cy = float(m_vpH)*0.5f;
    const float sx = (float(px.x()) - cx - m_panOffset.x()) / m_zoom + cx;
    const float sy = (float(px.y()) - cy - m_panOffset.y()) / m_zoom + cy;
    const float dx = sx - ox, dy = sy - oy;
    r = std::sqrt(dx*dx + dy*dy) / scale;
    float angle = qRadiansToDegrees(std::atan2(dy, dx));
    if (angle < 0.0f) angle += 360.0f;
    theta = angle;
    return (r >= 0.0f && r <= 1.0f);
}

bool PolarPyWidget::polarToCell(float r, float theta, int& ring, int& sector) const
{
    if (r < 0.0f || r > 1.0f) return false;
    const float span = m_endAngle - m_startAngle;
    float rel = theta - m_startAngle;
    while (rel <    0.0f) rel += 360.0f;
    while (rel >= 360.0f) rel -= 360.0f;
    if (rel < 0.0f || rel > span) return false;
    ring   = std::min(int(r   * float(m_radialBins)),  m_radialBins  - 1);
    sector = std::min(int(rel / span * float(m_angularBins)), m_angularBins - 1);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Events
// ─────────────────────────────────────────────────────────────────────────────
void PolarPyWidget::wheelEvent(QWheelEvent* e)
{
    const float factor = (e->angleDelta().y() > 0) ? 1.15f : 1.0f/1.15f;
    m_zoom = std::max(0.2f, std::min(10.0f, m_zoom*factor));
    update();
}

void PolarPyWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) { m_dragging = true; m_lastMousePos = e->pos(); }
}

void PolarPyWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->pos();
    if (m_dragging) { m_panOffset += QPointF(pos-m_lastMousePos); m_lastMousePos = pos; update(); return; }

    m_hoverPixel = pos;
    m_crosshairPixel = QPointF(pos);
    m_crosshairVisible = true;

    float r, theta;
    pixelToPolar(pos, r, theta);
    m_crosshairR     = r;
    m_crosshairTheta = theta;

    int ring = -1, sector = -1;
    if (pixelToPolar(pos, r, theta) && polarToCell(r, theta, ring, sector))
    {
        if (ring != m_hoveredRing || sector != m_hoveredSector)
        {
            m_hoveredRing = ring; m_hoveredSector = sector;
            if (!m_data.empty())
            {
                const int idx = ring*m_angularBins+sector;
                if (idx >= 0 && idx < int(m_data.size()))
                    emit sectorHovered(ring, sector, int(m_data[idx]));
            }
            emitTooltip();
        }
    }
    else if (m_hoveredRing >= 0 || m_hoveredSector >= 0)
    { m_hoveredRing = -1; m_hoveredSector = -1; emitTooltip(); }
    update();
}

void PolarPyWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_dragging = false;
        float r, theta;
        int ring, sector;
        if (pixelToPolar(e->pos(), r, theta) && polarToCell(r, theta, ring, sector))
        {
            m_selectedRing = ring; m_selectedSector = sector;
            m_vboDirty = true;
            if (!m_data.empty())
            {
                const int idx = ring*m_angularBins+sector;
                if (idx >= 0 && idx < int(m_data.size()))
                    emit sectorSelected(ring, sector, int(m_data[idx]));
            }
            update();
        }
    }
}

void PolarPyWidget::mouseDoubleClickEvent(QMouseEvent*) { resetView(); }

// FIX: crosshair and hover state cleared when mouse leaves widget
void PolarPyWidget::leaveEvent(QEvent*)
{
    m_crosshairVisible = false;
    m_hoveredRing      = -1;
    m_hoveredSector    = -1;
    emitTooltip();
    update();
}

void PolarPyWidget::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    QMenu* cursorMenu    = menu.addMenu("Cursor shape");
    QAction* actDefault  = cursorMenu->addAction("Default arrow");
    QAction* actCross    = cursorMenu->addAction("Crosshair");
    QAction* actTri      = cursorMenu->addAction("Triangle");
    QAction* actSect     = cursorMenu->addAction("Sector");
    menu.addSeparator();
    QAction* actMark1    = menu.addAction("Add Marker 1 (Triangle)");
    QAction* actMark2    = menu.addAction("Add Marker 2 (Mini Sector)");
    QAction* actClearMk  = menu.addAction("Clear all markers");
    menu.addSeparator();
    QAction* actReset    = menu.addAction("Reset view");

    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if      (chosen == actDefault) setCursorShape(CursorShape::Default);
    else if (chosen == actCross)   setCursorShape(CursorShape::Crosshair);
    else if (chosen == actTri)     setCursorShape(CursorShape::Triangle);
    else if (chosen == actSect)    setCursorShape(CursorShape::Sector);
    else if (chosen == actReset)   resetView();
    else if (chosen == actClearMk) { m_markers.clear(); update(); }
    else if (chosen == actMark1 || chosen == actMark2)
    {
        float r = 0.0f, theta = 0.0f;
        if (!pixelToPolar(e->pos(), r, theta)) return;
        PolarMarker marker;
        marker.type         = (chosen == actMark1) ? MarkerType::Triangle : MarkerType::MiniSector;
        marker.r            = r;
        marker.theta        = theta;
        marker.clickedPixel = QPointF(e->pos());
        marker.miniScale    = m_miniMarkerScale;
        if (marker.type == MarkerType::MiniSector)
        {
            bool ok = false;
            marker.outerMult = QInputDialog::getDouble(this, "Marker height", "Height (radial depth, e.g. 8):", 8.0, 1.0, 50.0, 1, &ok);
            marker.widthMult = QInputDialog::getDouble(this, "Marker width", "Width (angular span multiplier, e.g. 1):", 1.0, 0.2, 50.0, 1, &ok);
        }
        m_markers.push_back(marker);
        update();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Static paint helpers
// ─────────────────────────────────────────────────────────────────────────────
static void paintArcScaleLabels(QPainter& p,
                                 float ox, float oy, float scale,
                                 float startAngle, float endAngle,
                                 float minRange, float maxRange,
                                 float zoom, QPointF panOffset,
                                 int vpW, int vpH)
{
    static const float pcts[] = {0.0f, 25.0f, 50.0f, 75.0f, 100.0f};
    const float outerRadius = scale * 1.18f;
    const float spanDeg = endAngle - startAngle;
    const float rangeSpan = maxRange - minRange;
    const float cx = float(vpW)*0.5f, cy = float(vpH)*0.5f;
    QFont f("Monospace", 7); f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255,230,100,230));
    const QFontMetrics fm(f);
    for (float pct : pcts)
    {
        const float angleDeg = startAngle + (pct/100.0f)*spanDeg;
        const float angleRad = qDegreesToRadians(angleDeg);
        const float wx = ox + outerRadius*std::cos(angleRad);
        const float wy = oy + outerRadius*std::sin(angleRad);
        float lx = (wx-cx)*zoom + cx + panOffset.x();
        float ly = (wy-cy)*zoom + cy + panOffset.y();
        const QString lbl = QString::number(int(std::round(minRange + (pct/100.0f)*rangeSpan)));
        const int tw = fm.horizontalAdvance(lbl), th = fm.height();
        lx = std::max(float(tw)*0.5f+2.0f, std::min(lx, float(vpW)-float(tw)*0.5f-2.0f));
        ly = std::max(float(th)+2.0f, std::min(ly, float(vpH)-2.0f));
        float rot = angleDeg + 90.0f;
        rot = std::fmod(rot, 360.0f); if (rot < 0.0f) rot += 360.0f;
        if (rot > 90.0f && rot < 270.0f) rot += 180.0f;
        rot = std::fmod(rot, 360.0f);
        p.save(); p.translate(lx,ly); p.rotate(double(rot));
        p.drawText(-tw/2, 0, lbl); p.restore();
    }
}

static void paintExternalSpokeLabels(QPainter& p,
                                      float ox, float oy, float scale,
                                      float startAngle, float endAngle,
                                      int numSpokes,
                                      float zoom, QPointF panOffset,
                                      int vpW, int vpH)
{
    const float radius = scale * 1.12f;
    const float spanDeg = endAngle - startAngle;
    const float cx = float(vpW)*0.5f, cy = float(vpH)*0.5f;
    QFont f("Sans", 8);
    p.setFont(f);
    p.setPen(QColor(200,200,255,200));
    const QFontMetrics fm(f);
    for (int s = 0; s <= numSpokes; ++s)
    {
        const float angleDeg = startAngle + float(s)/float(numSpokes)*spanDeg;
        const float displayDeg = angleDeg + 90.0f;
        const float angleRad = qDegreesToRadians(angleDeg);
        const float wx = ox + radius*std::cos(angleRad);
        const float wy = oy + radius*std::sin(angleRad);
        float lx = (wx-cx)*zoom + cx + panOffset.x();
        float ly = (wy-cy)*zoom + cy + panOffset.y();
        const QString lbl = QString("%1\u00B0").arg(int(std::round(displayDeg)));
        const int tw = fm.horizontalAdvance(lbl), th = fm.height();
        lx -= tw*0.5f; ly += th*0.25f;
        lx = std::max(2.0f, std::min(lx, float(vpW-tw-2)));
        ly = std::max(float(th), std::min(ly, float(vpH-2)));
        p.drawText(QPointF(lx,ly), lbl);
    }
}

static void paintCrosshair(QPainter& p,
                            float ox, float oy, float scale,
                            float crossR, float crossTheta,
                            QPointF cursorPx,
                            int hoveredRing, int hoveredSector,
                            float hoveredValue,
                            float minRange, float maxRange,
                            float zoom, QPointF panOffset,
                            int vpW, int vpH)
{
    const float angleRad = qDegreesToRadians(crossTheta);
    const float cx = float(vpW)*0.5f, cy = float(vpH)*0.5f;
    auto toScreen = [&](float r, float ang) -> QPointF {
        const float wx = ox + r*scale*std::cos(ang);
        const float wy = oy + r*scale*std::sin(ang);
        return {(wx-cx)*zoom+cx+panOffset.x(), (wy-cy)*zoom+cy+panOffset.y()};
    };
    const QPointF originPx = toScreen(0.0f, 0.0f);
    const QPointF outerPx  = toScreen(1.0f, angleRad);
//    p.setPen(QPen(QColor(255,255,255,140), 1.0, Qt::DashLine));
//    p.drawLine(originPx, outerPx);
    p.setPen(QPen(QColor(0,220,255,100), 1.0, Qt::DotLine));
    const float ringRadius = std::hypot(double(cursorPx.x()-originPx.x()), double(cursorPx.y()-originPx.y()));
    p.drawEllipse(originPx, double(ringRadius), double(ringRadius));
    p.drawLine(QPointF(cursorPx.x(),0), QPointF(cursorPx.x(),float(vpH)));
    QString info;
    if (hoveredRing >= 0 && hoveredSector >= 0)
        info = QString("R%1 S%2  val=%3  r=%4  \u03B8=%5\u00B0")
            .arg(hoveredRing,2,10,QLatin1Char('0')).arg(hoveredSector,2,10,QLatin1Char('0'))
            .arg(hoveredValue,0,'f',1).arg(crossR,0,'f',3).arg(crossTheta,0,'f',1);
    else
        info = QString("r=%1  \u03B8=%2\u00B0").arg(crossR,0,'f',3).arg(crossTheta,0,'f',1);
    QFont f("Monospace", 8);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int tw = fm.horizontalAdvance(info), th = fm.height();
    int ix = int(cursorPx.x())+16, iy = int(cursorPx.y())-6;
    ix = std::min(ix, vpW-tw-6); iy = std::max(iy, th+2);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0,0,0,170));
    p.drawRoundedRect(ix-3, iy-th, tw+8, th+4, 3, 3);
    p.setPen(QColor(0,220,255,240));
    p.drawText(ix, iy, info);
    Q_UNUSED(minRange); Q_UNUSED(maxRange);
}

// ─────────────────────────────────────────────────────────────────────────────
// Markers
// ─────────────────────────────────────────────────────────────────────────────
void PolarPyWidget::drawMarkers(QPainter& p)
{
    if (m_markers.empty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);
    const float vpCx = float(m_vpW)*0.5f, vpCy = float(m_vpH)*0.5f;
    auto toScreen = [&](float r, float deg, float& sx, float& sy) {
        const float rad = qDegreesToRadians(deg);
        const float wx = ox + r*scale*std::cos(rad);
        const float wy = oy + r*scale*std::sin(rad);
        sx = (wx-vpCx)*m_zoom + vpCx + float(m_panOffset.x());
        sy = (wy-vpCy)*m_zoom + vpCy + float(m_panOffset.y());
    };
    for (int i = 0; i < int(m_markers.size()); ++i)
    {
        const PolarMarker& mk = m_markers[i];
        float cx, cy;
        toScreen(mk.r, mk.theta, cx, cy);
        const QColor col = (mk.type == MarkerType::Triangle)
                           ? QColor(255,180,30,230) : QColor(30,220,220,230);
        if (mk.type == MarkerType::Triangle)
        {
            const float sz = 10.0f*m_zoom, h = sz*0.866f;
            QPolygonF tri;
            tri << QPointF(cx, cy-h*0.667f) << QPointF(cx-sz*0.5f, cy+h*0.333f) << QPointF(cx+sz*0.5f, cy+h*0.333f);
            p.setPen(QPen(col.darker(150), 1.2)); p.setBrush(QBrush(col));
            p.drawPolygon(tri);
            p.setPen(QColor(255,255,255,220));
            p.setFont(QFont("Monospace",7,QFont::Bold));
            p.drawText(QPointF(cx+sz*0.5f+2, cy-h*0.667f), QString("M%1").arg(i+1));
        }
        else
        {
            const float span   = m_endAngle - m_startAngle;
            const float dTheta = (m_angularBins > 0) ? span/float(m_angularBins) : 10.0f;
            const float ringDepthPx = scale*m_zoom*(1.0f/std::max(m_radialBins,1))*mk.miniScale;
            const float halfDeg = dTheta*0.5f*mk.widthMult;
            const float outerR  = ringDepthPx*mk.outerMult, innerR = ringDepthPx*2.0f;
            QPainterPath path;
            const int steps = 12;
            for (int s = 0; s <= steps; ++s)
            {
                const float ang = qDegreesToRadians(mk.theta-halfDeg + float(s)/float(steps)*dTheta);
                const float px = cx+outerR*std::cos(ang), py = cy+outerR*std::sin(ang);
                if (s==0) path.moveTo(px,py); else path.lineTo(px,py);
            }
            for (int s = steps; s >= 0; --s)
            {
                const float ang = qDegreesToRadians(mk.theta-halfDeg + float(s)/float(steps)*dTheta);
                path.lineTo(cx+innerR*std::cos(ang), cy+innerR*std::sin(ang));
            }
            path.closeSubpath();
            p.setPen(QPen(col.darker(150),1.2)); p.setBrush(QBrush(col));
            p.drawPath(path);
            p.setPen(QColor(255,255,255,220));
            p.setFont(QFont("Monospace",7,QFont::Bold));
            p.drawText(QPointF(cx+outerR+3, cy), QString("M%1").arg(i+1));
        }
    }
    p.restore();
}
