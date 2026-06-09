#include "polarpywidget.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float degToRad(float d)
{
    return d * static_cast<float>(M_PI) / 180.0f;
}

// ===========================================================
//  Vertex shader — receives position + colour per vertex
// ===========================================================
static const char* VS_SRC = R"(
#version 330 core
layout(location = 0) in vec2  aPos;
layout(location = 1) in vec4  aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main()
{
    vColor      = aColor;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

// ===========================================================
//  Fragment shader
// ===========================================================
static const char* FS_SRC = R"(
#version 330 core
in  vec4 vColor;
out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
)";

// ===========================================================
//  Constructor / Destructor  (Change 5)
// ===========================================================
PolarPyWidget::PolarPyWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // Request OpenGL 3.3 Core profile (Change 1)
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);   // MSAA
    setFormat(fmt);
}

PolarPyWidget::~PolarPyWidget()
{
    // Must make context current before deleting GL objects
    makeCurrent();

    // Clean up VBOs and VAOs
    m_sectorVBO.destroy();
    m_sectorVAO.destroy();
    m_gridVBO.destroy();
    m_gridVAO.destroy();

    // Clean up shader program
    delete m_program;
    m_program = nullptr;

    // Clean up texture (Change 4 + 5)
    delete m_bgTexture;
    m_bgTexture = nullptr;

    doneCurrent();
}

// ===========================================================
//  Public API
// ===========================================================
void PolarPyWidget::plotData(char* data, int radialBins, int angularBins)
{
    if (!data || radialBins <= 0 || angularBins <= 0) {
        m_errorMsg = "plotData: invalid arguments.";
        update();
        return;
    }
    m_radialBins  = radialBins;
    m_angularBins = angularBins;
    int total     = radialBins * angularBins;
    m_data.assign(reinterpret_cast<unsigned char*>(data),
                  reinterpret_cast<unsigned char*>(data) + total);
    m_errorMsg.clear();
    m_vboDirty = true;   // mark VBOs for rebuild
    update();
}

// ===========================================================
//  mapValueToColor  (Blue → Cyan → Green → Yellow → Red)
// ===========================================================
QColor PolarPyWidget::mapValueToColor(int value)
{
    float t = std::max(0, std::min(255, value)) / 255.0f;
    float r, g, b;
    if      (t < 0.25f){ float u = t/0.25f;         r=0; g=u;   b=1;   }
    else if (t < 0.5f) { float u = (t-0.25f)/0.25f; r=0; g=1;   b=1-u; }
    else if (t < 0.75f){ float u = (t-0.5f)/0.25f;  r=u; g=1;   b=0;   }
    else               { float u = (t-0.75f)/0.25f;  r=1; g=1-u; b=0;  }
    return QColor::fromRgbF(static_cast<double>(r),
                            static_cast<double>(g),
                            static_cast<double>(b));
}

void PolarPyWidget::setMinRange(float v)  { m_minRange   = v; m_vboDirty=true; update(); }
void PolarPyWidget::setMaxRange(float v)  { m_maxRange   = v; m_vboDirty=true; update(); }
void PolarPyWidget::setStartAngle(float a){ m_startAngle = a; m_vboDirty=true; update(); }
void PolarPyWidget::setEndAngle(float a)  { m_endAngle   = a; m_vboDirty=true; update(); }

bool PolarPyWidget::validate()
{
    if (m_data.empty())            { m_errorMsg="No data. Call plotData() first."; return false; }
    if (m_minRange >= m_maxRange)  { m_errorMsg="minRange must be < maxRange.";    return false; }
    if (m_startAngle >= m_endAngle){ m_errorMsg="startAngle must be < endAngle.";  return false; }
    m_errorMsg.clear();
    return true;
}

// ===========================================================
//  Change 6: Dynamic screen utilization
//  Computes scale and centre offset so the arc fills the
//  available widget area regardless of angular span.
// ===========================================================
void PolarPyWidget::computeDrawBounds(float& scaleOut,
                                       float& offsetXOut,
                                       float& offsetYOut) const
{
    float spanDeg = m_endAngle - m_startAngle;

    // Build bounding box of the arc extremes
    float minX =  1e9f, maxX = -1e9f;
    float minY =  1e9f, maxY = -1e9f;

    // Sample arc boundary points at full radius (normalised r=1)
    int steps = 360;
    for (int i = 0; i <= steps; ++i) {
        float ang = degToRad(m_startAngle + spanDeg * float(i) / steps);
        float x   = std::cos(ang);
        float y   = std::sin(ang);
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    // Also include origin (inner edge)
    minX = std::min(minX, 0.0f); maxX = std::max(maxX, 0.0f);
    minY = std::min(minY, 0.0f); maxY = std::max(maxY, 0.0f);

    float rangeW = maxX - minX;
    float rangeH = maxY - minY;
    if (rangeW < 1e-4f) rangeW = 1e-4f;
    if (rangeH < 1e-4f) rangeH = 1e-4f;

    float margin  = 40.0f;   // pixels margin for labels
    float availW  = float(m_width)  - margin * 2;
    float availH  = float(m_height) - margin * 2;

    // Scale: fit the bounding box into available space
    float scaleX  = availW / (rangeW * m_maxRange);
    float scaleY  = availH / (rangeH * m_maxRange);
    scaleOut      = std::min(scaleX, scaleY);

    // Offset: centre the bounding box in the widget
    float drawW   = rangeW * m_maxRange * scaleOut;
    float drawH   = rangeH * m_maxRange * scaleOut;

    // In GL coords (origin = widget centre, y-up):
    offsetXOut = -((minX + maxX) * 0.5f) * m_maxRange * scaleOut;
    offsetYOut = -((minY + maxY) * 0.5f) * m_maxRange * scaleOut;

    (void)drawW; (void)drawH;  // suppress warnings
}

// ===========================================================
//  Shader initialisation  (Change 1 + 3)
// ===========================================================
bool PolarPyWidget::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   VS_SRC) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, FS_SRC) ||
        !m_program->link()) {
        m_errorMsg = "Shader error: " + m_program->log().toStdString();
        return false;
    }
    m_uMVP      = m_program->uniformLocation("uMVP");
    return true;
}

// ===========================================================
//  initializeGL
// ===========================================================
void PolarPyWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.07f, 0.07f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initShaders();

    // Change 4: create a simple procedural background texture
    // (concentric faint circles on a dark gradient — radar style)
    {
        const int SZ = 256;
        QImage img(SZ, SZ, QImage::Format_RGBA8888);
        for (int y = 0; y < SZ; ++y) {
            for (int x = 0; x < SZ; ++x) {
                float dx = (x - SZ/2.0f) / (SZ/2.0f);
                float dy = (y - SZ/2.0f) / (SZ/2.0f);
                float d  = std::sqrt(dx*dx + dy*dy);
                // Faint rings at 0.25, 0.5, 0.75, 1.0 normalised radius
                float ring = std::fmod(d * 4.0f, 1.0f);
                int alpha  = (ring > 0.92f) ? 30 : 0;
                img.setPixelColor(x, y, QColor(80, 120, 200, alpha));
            }
        }
        m_bgTexture = new QOpenGLTexture(img.mirrored());
        m_bgTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_bgTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    }
}

// ===========================================================
//  resizeGL
// ===========================================================
void PolarPyWidget::resizeGL(int width, int height)
{
    m_width  = width;
    m_height = height;
    glViewport(0, 0, width, height);

    // Orthographic projection: pixel coords, origin at centre, y-up
    m_projection.setToIdentity();
    m_projection.ortho(
        -width  * 0.5f,  width  * 0.5f,
        -height * 0.5f,  height * 0.5f,
        -1.0f, 1.0f);

    m_vboDirty = true;
}

// ===========================================================
//  buildSectorVBO  (Change 3: replace glBegin with VBO)
//
//  Layout per vertex: X(f) Y(f) R(f) G(f) B(f) A(f)
//  One triangle fan per sector = (ARC_STEPS+2)*2 triangles
//  We use GL_TRIANGLES so it packs into a flat buffer.
// ===========================================================
void PolarPyWidget::buildSectorVBO()
{
    float scale, offsetX, offsetY;
    computeDrawBounds(scale, offsetX, offsetY);

    float rangeSpan = m_maxRange - m_minRange;
    float angleSpan = m_endAngle  - m_startAngle;

    // Each sector: (ARC_STEPS+1)*2 vertices forming a TRIANGLE_STRIP
    // 6 floats per vertex (x, y, r, g, b, a)
    std::vector<float> verts;
    verts.reserve(size_t(m_radialBins) * m_angularBins
                  * (ARC_STEPS + 2) * 2 * 6);

    for (int r = 0; r < m_radialBins; ++r) {
        float innerR = (m_minRange + rangeSpan * float(r)     / m_radialBins) * scale;
        float outerR = (m_minRange + rangeSpan * float(r + 1) / m_radialBins) * scale;

        for (int s = 0; s < m_angularBins; ++s) {
            float a0 = m_startAngle + angleSpan * float(s)     / m_angularBins;
            float a1 = m_startAngle + angleSpan * float(s + 1) / m_angularBins;

            QColor col = mapValueToColor(m_data[r * m_angularBins + s]);
            float  cr  = float(col.redF());
            float  cg  = float(col.greenF());
            float  cb  = float(col.blueF());
            float  ca  = 0.88f;

            // TRIANGLE_STRIP: alternate outer/inner arc vertices
            for (int i = 0; i <= ARC_STEPS; ++i) {
                float ang = degToRad(a0 + (a1 - a0) * float(i) / ARC_STEPS);
                float ca_ = std::cos(ang);
                float sa_ = std::sin(ang);

                // outer vertex
                verts.push_back(outerR * ca_ + offsetX);
                verts.push_back(outerR * sa_ + offsetY);
                verts.push_back(cr); verts.push_back(cg);
                verts.push_back(cb); verts.push_back(ca);

                // inner vertex
                verts.push_back(innerR * ca_ + offsetX);
                verts.push_back(innerR * sa_ + offsetY);
                verts.push_back(cr * 0.65f); verts.push_back(cg * 0.65f);
                verts.push_back(cb * 0.65f); verts.push_back(ca);
            }
        }
    }

    m_sectorVertexCount = int(verts.size()) / 6;

    if (!m_sectorVAO.isCreated()) m_sectorVAO.create();
    m_sectorVAO.bind();

    if (!m_sectorVBO.isCreated())
        m_sectorVBO.create();
    m_sectorVBO.bind();
    m_sectorVBO.allocate(verts.data(),
                         int(verts.size() * sizeof(float)));

    // Attribute 0: position (xy)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    // Attribute 1: colour (rgba)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    m_sectorVAO.release();
    m_sectorVBO.release();
}

// ===========================================================
//  buildGridVBO  (Change 3)
// ===========================================================
void PolarPyWidget::buildGridVBO()
{
    float scale, offsetX, offsetY;
    computeDrawBounds(scale, offsetX, offsetY);

    float rangeSpan = m_maxRange - m_minRange;
    float angleSpan = m_endAngle  - m_startAngle;
    const int ARC   = 180;

    std::vector<float> verts;

    // ---- Rings ----
    for (int r = 0; r <= m_radialBins; ++r) {
        float rad   = (m_minRange + rangeSpan * float(r) / m_radialBins) * scale;
        bool  outer = (r == m_radialBins);
        float alpha = outer ? 0.70f : 0.28f;
        float col   = outer ? 0.85f : 0.55f;

        for (int i = 0; i < ARC; ++i) {
            for (int k = 0; k < 2; ++k) {
                float ang = degToRad(m_startAngle + angleSpan * float(i+k) / ARC);
                verts.push_back(rad * std::cos(ang) + offsetX);
                verts.push_back(rad * std::sin(ang) + offsetY);
                verts.push_back(0.4f); verts.push_back(0.5f);
                verts.push_back(col);  verts.push_back(alpha);
            }
        }
    }

    // ---- Spokes ----
    float innerR = m_minRange * scale;
    float outerR = m_maxRange * scale;
    for (int s = 0; s <= m_angularBins; ++s) {
        float ang = degToRad(m_startAngle + angleSpan * float(s) / m_angularBins);
        float ca  = std::cos(ang), sa = std::sin(ang);

        // inner vertex (transparent)
        verts.push_back(innerR * ca + offsetX);
        verts.push_back(innerR * sa + offsetY);
        verts.push_back(0.4f); verts.push_back(0.5f); verts.push_back(0.8f); verts.push_back(0.12f);

        // outer vertex (more visible)
        verts.push_back(outerR * ca + offsetX);
        verts.push_back(outerR * sa + offsetY);
        verts.push_back(0.4f); verts.push_back(0.5f); verts.push_back(0.8f); verts.push_back(0.45f);
    }

    m_gridVertexCount = int(verts.size()) / 6;

    if (!m_gridVAO.isCreated()) m_gridVAO.create();
    m_gridVAO.bind();

    if (!m_gridVBO.isCreated()) m_gridVBO.create();
    m_gridVBO.bind();
    m_gridVBO.allocate(verts.data(), int(verts.size() * sizeof(float)));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    m_gridVAO.release();
    m_gridVBO.release();
}

// ===========================================================
//  paintGL
// ===========================================================
void PolarPyWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (!validate()) {
        QPainter p(this);
        p.setPen(QColor(255, 80, 80));
        p.setFont(QFont("Monospace", 11));
        p.drawText(rect(), Qt::AlignCenter,
                   QString::fromStdString(m_errorMsg));
        return;
    }

    // Rebuild VBOs if data or config changed
    if (m_vboDirty) {
        buildSectorVBO();
        buildGridVBO();
        m_vboDirty = false;
    }

    if (!m_program) return;
    m_program->bind();
    m_program->setUniformValue(m_uMVP, m_projection);

    drawSectors();
    drawGrid();

    m_program->release();

    // QPainter overlay: range labels + footer
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    drawRangeLabels();   // Change 2
    (void)p;             // p used inside drawRangeLabels via separate QPainter
}

// ===========================================================
//  drawSectors  (Change 3: draw from VBO)
// ===========================================================
void PolarPyWidget::drawSectors()
{
    m_sectorVAO.bind();
    // Each sector uses (ARC_STEPS+1)*2 vertices as TRIANGLE_STRIP
    int vertsPerSector = (ARC_STEPS + 1) * 2;
    int sectorCount    = m_radialBins * m_angularBins;
    for (int i = 0; i < sectorCount; ++i)
        glDrawArrays(GL_TRIANGLE_STRIP,
                     i * vertsPerSector,
                     vertsPerSector);
    m_sectorVAO.release();
}

// ===========================================================
//  drawGrid  (Change 3: draw from VBO)
// ===========================================================
void PolarPyWidget::drawGrid()
{
    m_gridVAO.bind();

    const int ARC        = 180;
    int ringsVertices    = (m_radialBins + 1) * ARC * 2;
    int spokesVertices   = (m_angularBins + 1) * 2;

    // Rings as GL_LINES pairs
    glDrawArrays(GL_LINES, 0, ringsVertices);
    // Spokes as GL_LINES pairs
    glDrawArrays(GL_LINES, ringsVertices, spokesVertices);

    m_gridVAO.release();
}

// ===========================================================
//  drawRangeLabels  (Change 2)
//  Draws distance values on each concentric ring.
// ===========================================================
void PolarPyWidget::drawRangeLabels()
{
    float scale, offsetX, offsetY;
    computeDrawBounds(scale, offsetX, offsetY);

    // Label position: along the start-angle spoke
    float labelAngle = degToRad(m_startAngle);
    float ca = std::cos(labelAngle);
    float sa = std::sin(labelAngle);

    // GL coords → widget pixel coords
    // GL origin = widget centre, y-up
    float cx = m_width  * 0.5f;
    float cy = m_height * 0.5f;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(QFont("Monospace", 9));

    float rangeSpan = m_maxRange - m_minRange;

    for (int r = 1; r <= m_radialBins; ++r) {
        float rangeVal = m_minRange + rangeSpan * float(r) / m_radialBins;
        float rad      = rangeVal * scale;

        // Convert GL position to pixel
        float glx = rad * ca + offsetX;
        float gly = rad * sa + offsetY;
        float px  = cx + glx;
        float py  = cy - gly;   // flip y for screen coords

        QString lbl = QString::number(int(rangeVal));

        // White text with dark outline for readability over heatmap
        p.setPen(QColor(0, 0, 0, 120));
        p.drawText(QPointF(px + 1, py + 1), lbl);
        p.setPen(QColor(220, 235, 255));
        p.drawText(QPointF(px, py), lbl);
    }

    // Footer
    p.setPen(QColor(80, 100, 160, 140));
    p.setFont(QFont("Monospace", 8));
    p.drawText(QRect(4, m_height - 18, 500, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString("PolarPyWidget  rings=%1  sectors=%2  "
                       "angle=%3°–%4°  OpenGL 3.3 Core")
                   .arg(m_radialBins).arg(m_angularBins)
                   .arg(int(m_startAngle)).arg(int(m_endAngle)));
}
