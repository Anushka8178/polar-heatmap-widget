#include "polarpywidget.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QImage>
#include <cmath>
#include <algorithm>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float deg2rad(float d){ return d * float(M_PI) / 180.0f; }

// ─────────────────────────────────────────────────────────────
//  GLSL Shaders  (Change 1 — OpenGL 3.3 Core)
// ─────────────────────────────────────────────────────────────
static const char* VS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main(){
    vColor      = aColor;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

static const char* FS = R"(
#version 330 core
in  vec4 vColor;
out vec4 fragColor;
void main(){ fragColor = vColor; }
)";

// ─────────────────────────────────────────────────────────────
//  Constructor  (Change 1 — request 3.3 Core)
// ─────────────────────────────────────────────────────────────
PolarPyWidget::PolarPyWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMouseTracking(true);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    setFormat(fmt);
}

// ─────────────────────────────────────────────────────────────
//  Destructor  (Change 5 — full GL cleanup)
// ─────────────────────────────────────────────────────────────
PolarPyWidget::~PolarPyWidget()
{
    makeCurrent();
    m_sectorVBO.destroy();
    m_sectorVAO.destroy();
    m_gridVBO.destroy();
    m_gridVAO.destroy();
    delete m_program;   m_program   = nullptr;
    delete m_bgTexture; m_bgTexture = nullptr;
    doneCurrent();
}

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::plotData(char* data, int radialBins, int angularBins)
{
    if (!data || radialBins <= 0 || angularBins <= 0){
        m_errorMsg = "plotData: invalid arguments."; update(); return;
    }
    m_radialBins  = radialBins;
    m_angularBins = angularBins;
    m_data.assign(reinterpret_cast<unsigned char*>(data),
                  reinterpret_cast<unsigned char*>(data) + radialBins * angularBins);
    m_errorMsg.clear();
    m_vboDirty = true;
    update();
}

std::vector<unsigned char> PolarPyWidget::generateTestData(int rows, int cols)
{
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> d(0, 255);
    std::vector<unsigned char> buf(rows * cols);
    for (auto& v : buf) v = static_cast<unsigned char>(d(rng));
    return buf;
}

QColor PolarPyWidget::mapValueToColor(int value)
{
    float t = std::max(0, std::min(255, value)) / 255.0f;
    float r, g, b;
    if      (t < 0.25f){ float u=t/0.25f;         r=0; g=u;   b=1;   }
    else if (t < 0.50f){ float u=(t-0.25f)/0.25f; r=0; g=1;   b=1-u; }
    else if (t < 0.75f){ float u=(t-0.50f)/0.25f; r=u; g=1;   b=0;   }
    else               { float u=(t-0.75f)/0.25f;  r=1; g=1-u; b=0;  }
    return QColor::fromRgbF(double(r), double(g), double(b));
}

void PolarPyWidget::setMinRange(float v)  { m_minRange   = v; m_vboDirty=true; update(); }
void PolarPyWidget::setMaxRange(float v)  { m_maxRange   = v; m_vboDirty=true; update(); }
void PolarPyWidget::setStartAngle(float a){ m_startAngle = a; m_vboDirty=true; update(); }
void PolarPyWidget::setEndAngle(float a)  { m_endAngle   = a; m_vboDirty=true; update(); }
void PolarPyWidget::resetView()           { m_zoom=1.0f; m_panOffset={0,0}; update(); }

// ─────────────────────────────────────────────────────────────
//  Validation
// ─────────────────────────────────────────────────────────────
bool PolarPyWidget::validate()
{
    if (m_data.empty())            { m_errorMsg="No data — call plotData() first."; return false; }
    if (m_minRange >= m_maxRange)  { m_errorMsg="minRange must be < maxRange.";     return false; }
    if (m_startAngle >= m_endAngle){ m_errorMsg="startAngle must be < endAngle.";   return false; }
    m_errorMsg.clear(); return true;
}

// ─────────────────────────────────────────────────────────────
//  Change 6 — computeDrawBounds
//  Bounding-box of the actual arc → scale + offset to fill widget
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::computeDrawBounds(float& scaleOut,
                                       float& oxOut, float& oyOut) const
{
    float span = m_endAngle - m_startAngle;
    float mnX= 1e9f, mxX=-1e9f, mnY= 1e9f, mxY=-1e9f;
    for (int i = 0; i <= 360; ++i){
        float a = deg2rad(m_startAngle + span * float(i) / 360);
        float x = std::cos(a), y = std::sin(a);
        mnX=std::min(mnX,x); mxX=std::max(mxX,x);
        mnY=std::min(mnY,y); mxY=std::max(mxY,y);
    }
    mnX=std::min(mnX,0.f); mxX=std::max(mxX,0.f);
    mnY=std::min(mnY,0.f); mxY=std::max(mxY,0.f);

    float rW = std::max(1e-4f, mxX-mnX);
    float rH = std::max(1e-4f, mxY-mnY);
    float margin = 44.0f;
    float aW = float(m_vpW) - margin*2;
    float aH = float(m_vpH) - margin*2;

    scaleOut = std::min(aW/(rW*m_maxRange), aH/(rH*m_maxRange));
    oxOut    = -((mnX+mxX)*0.5f) * m_maxRange * scaleOut;
    oyOut    = -((mnY+mxY)*0.5f) * m_maxRange * scaleOut;
}

// ─────────────────────────────────────────────────────────────
//  Shader init
// ─────────────────────────────────────────────────────────────
bool PolarPyWidget::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);
    bool ok = m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   VS)
           && m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, FS)
           && m_program->link();
    if (!ok){ m_errorMsg = "Shader error: " + m_program->log().toStdString(); return false; }
    m_uMVP = m_program->uniformLocation("uMVP");
    return true;
}

// ─────────────────────────────────────────────────────────────
//  initializeGL
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.06f, 0.06f, 0.10f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    initShaders();

    // Change 4 — Qt background texture (radar-style procedural rings)
    const int SZ = 256;
    QImage img(SZ, SZ, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    for (int y = 0; y < SZ; ++y)
        for (int x = 0; x < SZ; ++x){
            float dx = (x - SZ*0.5f)/(SZ*0.5f);
            float dy = (y - SZ*0.5f)/(SZ*0.5f);
            float d  = std::sqrt(dx*dx + dy*dy);
            float ring = std::fmod(d * 4.0f, 1.0f);
            if (ring > 0.90f)
                img.setPixelColor(x, y, QColor(80, 140, 220, 28));
        }
    m_bgTexture = new QOpenGLTexture(img.mirrored());
    m_bgTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_bgTexture->setMagnificationFilter(QOpenGLTexture::Linear);
}

// ─────────────────────────────────────────────────────────────
//  resizeGL
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::resizeGL(int w, int h)
{
    m_vpW = w; m_vpH = h;
    glViewport(0, 0, w, h);
    m_projection.setToIdentity();
    m_projection.ortho(-w*.5f, w*.5f, -h*.5f, h*.5f, -1, 1);
    m_vboDirty = true;
}

// ─────────────────────────────────────────────────────────────
//  Change 3 — buildSectorVBO
//  GL_TRIANGLE_STRIP per sector, alternating outer/inner verts
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::buildSectorVBO()
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    float rSpan = m_maxRange  - m_minRange;
    float aSpan = m_endAngle  - m_startAngle;

    std::vector<float> verts;
    verts.reserve(size_t(m_radialBins)*m_angularBins*(ARC_STEPS+1)*2*6);

    for (int r = 0; r < m_radialBins; ++r){
        float iR = (m_minRange + rSpan*float(r)  /m_radialBins)*scale;
        float oR = (m_minRange + rSpan*float(r+1)/m_radialBins)*scale;

        for (int s = 0; s < m_angularBins; ++s){
            float a0 = m_startAngle + aSpan*float(s)  /m_angularBins;
            float a1 = m_startAngle + aSpan*float(s+1)/m_angularBins;

            QColor col = mapValueToColor(m_data[r*m_angularBins+s]);
            float cr=float(col.redF()), cg=float(col.greenF()),
                  cb=float(col.blueF()), ca=0.90f;

            bool isSel = (r==m_selectedRing && s==m_selectedSector);
            if (isSel){ cr=std::min(1.f,cr*1.6f); cg=std::min(1.f,cg*1.6f); cb=std::min(1.f,cb*1.6f); }

            for (int i = 0; i <= ARC_STEPS; ++i){
                float ang = deg2rad(a0+(a1-a0)*float(i)/ARC_STEPS);
                float c_  = std::cos(ang), s_ = std::sin(ang);
                // outer
                verts.push_back(oR*c_+ox); verts.push_back(oR*s_+oy);
                verts.push_back(cr); verts.push_back(cg); verts.push_back(cb); verts.push_back(ca);
                // inner (darkened for depth gradient)
                verts.push_back(iR*c_+ox); verts.push_back(iR*s_+oy);
                verts.push_back(cr*0.55f); verts.push_back(cg*0.55f); verts.push_back(cb*0.55f); verts.push_back(ca);
            }
        }
    }

    m_sectorVertexCount = int(verts.size())/6;

    if (!m_sectorVAO.isCreated()) m_sectorVAO.create();
    m_sectorVAO.bind();
    if (!m_sectorVBO.isCreated()) m_sectorVBO.create();
    m_sectorVBO.bind();
    m_sectorVBO.allocate(verts.data(), int(verts.size()*sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    m_sectorVAO.release(); m_sectorVBO.release();
}

// ─────────────────────────────────────────────────────────────
//  Change 3 — buildGridVBO
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::buildGridVBO()
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);
    float rSpan = m_maxRange - m_minRange;
    float aSpan = m_endAngle - m_startAngle;

    std::vector<float> verts;

    // Rings
    m_gridRingVerts = 0;
    for (int r = 0; r <= m_radialBins; ++r){
        float rad   = (m_minRange + rSpan*float(r)/m_radialBins)*scale;
        bool  outer = (r==m_radialBins);
        float al    = outer?0.72f:0.28f;
        float br    = outer?0.88f:0.55f;
        for (int i = 0; i < GRID_ARC; ++i){
            for (int k = 0; k < 2; ++k){
                float ang = deg2rad(m_startAngle + aSpan*float(i+k)/GRID_ARC);
                verts.push_back(rad*std::cos(ang)+ox);
                verts.push_back(rad*std::sin(ang)+oy);
                verts.push_back(0.38f); verts.push_back(0.50f); verts.push_back(br); verts.push_back(al);
                m_gridRingVerts++;
            }
        }
    }

    // Spokes
    float iR = m_minRange*scale, oR = m_maxRange*scale;
    m_gridSpokeVerts = 0;
    for (int s = 0; s <= m_angularBins; ++s){
        float ang = deg2rad(m_startAngle + aSpan*float(s)/m_angularBins);
        float ca  = std::cos(ang), sa = std::sin(ang);
        verts.push_back(iR*ca+ox); verts.push_back(iR*sa+oy);
        verts.push_back(0.38f); verts.push_back(0.50f); verts.push_back(0.82f); verts.push_back(0.10f);
        verts.push_back(oR*ca+ox); verts.push_back(oR*sa+oy);
        verts.push_back(0.38f); verts.push_back(0.50f); verts.push_back(0.82f); verts.push_back(0.48f);
        m_gridSpokeVerts += 2;
    }

    if (!m_gridVAO.isCreated()) m_gridVAO.create();
    m_gridVAO.bind();
    if (!m_gridVBO.isCreated()) m_gridVBO.create();
    m_gridVBO.bind();
    m_gridVBO.allocate(verts.data(), int(verts.size()*sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    m_gridVAO.release(); m_gridVBO.release();
}

// ─────────────────────────────────────────────────────────────
//  paintGL
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (!validate()){
        QPainter p(this);
        p.setPen(QColor(255,80,80));
        p.setFont(QFont("Monospace",11));
        p.drawText(rect(), Qt::AlignCenter, QString::fromStdString(m_errorMsg));
        return;
    }

    if (m_vboDirty){ buildSectorVBO(); buildGridVBO(); m_vboDirty=false; }
    if (!m_program) return;

    // Apply zoom + pan via MVP
    QMatrix4x4 mvp = m_projection;
    mvp.translate(float(m_panOffset.x()), float(m_panOffset.y()), 0);
    mvp.scale(m_zoom, m_zoom, 1);

    m_program->bind();
    m_program->setUniformValue(m_uMVP, mvp);
    drawSectors();
    drawGrid();
    m_program->release();

    // QPainter overlays
    drawRangeLabels();
    drawOverlay();
}

void PolarPyWidget::drawSectors()
{
    m_sectorVAO.bind();
    int vps = (ARC_STEPS+1)*2;
    for (int i = 0; i < m_radialBins*m_angularBins; ++i)
        glDrawArrays(GL_TRIANGLE_STRIP, i*vps, vps);
    m_sectorVAO.release();
}

void PolarPyWidget::drawGrid()
{
    m_gridVAO.bind();
    glDrawArrays(GL_LINES, 0,                m_gridRingVerts);
    glDrawArrays(GL_LINES, m_gridRingVerts,  m_gridSpokeVerts);
    m_gridVAO.release();
}

// ─────────────────────────────────────────────────────────────
//  Change 2 — drawRangeLabels
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::drawRangeLabels()
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    float labelAng = deg2rad(m_startAngle);
    float ca = std::cos(labelAng), sa = std::sin(labelAng);
    float cx = m_vpW*0.5f, cy = m_vpH*0.5f;
    float rSpan = m_maxRange - m_minRange;

    // Account for zoom/pan in label positions
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(QFont("Monospace", 9));

    for (int r = 1; r <= m_radialBins; ++r){
        float rv  = m_minRange + rSpan*float(r)/m_radialBins;
        float rad = rv * scale;
        float glx = (rad*ca + ox)*m_zoom + float(m_panOffset.x());
        float gly = (rad*sa + oy)*m_zoom + float(m_panOffset.y());
        float px  = cx + glx;
        float py  = cy - gly;

        QString lbl = QString::number(int(rv));
        p.setPen(QColor(0,0,0,110));
        p.drawText(QPointF(px+1, py+1), lbl);
        p.setPen(QColor(215,230,255));
        p.drawText(QPointF(px, py), lbl);
    }

    // Footer bar
    p.setPen(QColor(70,90,150,130));
    p.setFont(QFont("Monospace",8));
    p.drawText(QRect(4, m_vpH-18, m_vpW-8, 16),
               Qt::AlignLeft|Qt::AlignVCenter,
               QString("PolarPyWidget  rings=%1  sectors=%2  %3°–%4°  zoom=×%5  OpenGL 3.3 Core | VBO")
                   .arg(m_radialBins).arg(m_angularBins)
                   .arg(int(m_startAngle)).arg(int(m_endAngle))
                   .arg(double(m_zoom),0,'f',2));
}

// ─────────────────────────────────────────────────────────────
//  drawOverlay — hover tooltip + selected sector outline
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::drawOverlay()
{
    if (m_hoveredRing < 0 && m_selectedRing < 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_hoveredRing >= 0 && m_hoveredSector >= 0){
        int val = m_data[m_hoveredRing*m_angularBins + m_hoveredSector];
        QColor hc = mapValueToColor(val);
        QString txt = QString("  Ring: %1   Sector: %2   Value: %3")
                          .arg(m_hoveredRing).arg(m_hoveredSector).arg(val);

        QFont f("Monospace",10); f.setBold(true); p.setFont(f);
        QFontMetrics fm(f);
        QRect tr = fm.boundingRect(txt);
        tr.setWidth(tr.width()+24); tr.setHeight(tr.height()+14);
        tr.moveTopLeft(QPoint(m_hoverPixel.x()+16, m_hoverPixel.y()-10));
        if (tr.right()  > m_vpW-4) tr.moveRight(m_vpW-4);
        if (tr.top()    < 4)       tr.moveTop(4);
        if (tr.bottom() > m_vpH-4) tr.moveBottom(m_vpH-4);

        p.setBrush(QColor(8,8,22,215));
        p.setPen(QPen(hc.lighter(160), 1.2));
        p.drawRoundedRect(tr, 6, 6);
        p.setBrush(hc); p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(tr.left()+3, tr.top()+4, 4, tr.height()-8), 2, 2);
        p.setPen(QColor(210,225,255));
        p.drawText(tr.adjusted(10,0,0,0), Qt::AlignVCenter|Qt::AlignLeft, txt);
    }
}

// ─────────────────────────────────────────────────────────────
//  Coordinate helpers
// ─────────────────────────────────────────────────────────────
bool PolarPyWidget::pixelToPolar(const QPoint& pixel, float& r, float& theta) const
{
    float scale, ox, oy;
    computeDrawBounds(scale, ox, oy);

    // Reverse MVP: pixel → GL data space
    float cx = m_vpW*0.5f, cy = m_vpH*0.5f;
    float glx = ((pixel.x()-cx) - float(m_panOffset.x())) / m_zoom;
    float gly = (-(pixel.y()-cy) - float(m_panOffset.y())) / m_zoom;

    float rx = (glx - ox) / scale;
    float ry = (gly - oy) / scale;

    r     = std::sqrt(rx*rx + ry*ry);
    theta = std::atan2(ry, rx) * 180.0f / float(M_PI);
    return (r >= m_minRange && r <= m_maxRange);
}

bool PolarPyWidget::polarToCell(float r, float theta, int& ring, int& sector) const
{
    if (r < m_minRange || r > m_maxRange) return false;
    while (theta <  m_startAngle) theta += 360.0f;
    while (theta >= m_endAngle)   theta -= 360.0f;
    if (theta < m_startAngle || theta >= m_endAngle) return false;
    float rS = m_maxRange-m_minRange, aS = m_endAngle-m_startAngle;
    ring   = std::max(0,std::min(int((r-m_minRange)/rS*m_radialBins),  m_radialBins-1));
    sector = std::max(0,std::min(int((theta-m_startAngle)/aS*m_angularBins), m_angularBins-1));
    return true;
}

// ─────────────────────────────────────────────────────────────
//  Mouse events
// ─────────────────────────────────────────────────────────────
void PolarPyWidget::wheelEvent(QWheelEvent* e)
{
    float f = (e->angleDelta().y()>0) ? 1.12f : 1.0f/1.12f;
    m_zoom  = std::max(0.1f, std::min(m_zoom*f, 20.0f));
    update();
}

void PolarPyWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button()==Qt::LeftButton){
        float r, theta;
        if (pixelToPolar(e->pos(), r, theta)){
            int ring, sec;
            if (polarToCell(r, theta, ring, sec)){
                m_selectedRing=ring; m_selectedSector=sec;
                m_vboDirty=true;
                emit sectorSelected(ring, sec, m_data[ring*m_angularBins+sec]);
                update(); return;
            }
        }
        m_dragging=true; m_lastMousePos=e->pos();
    }
}

void PolarPyWidget::mouseMoveEvent(QMouseEvent* e)
{
    m_hoverPixel = e->pos();
    float r, theta;
    if (pixelToPolar(e->pos(), r, theta)){
        int ring, sec;
        if (polarToCell(r, theta, ring, sec)){
            if (ring!=m_hoveredRing || sec!=m_hoveredSector){
                m_hoveredRing=ring; m_hoveredSector=sec;
                emit sectorHovered(ring, sec, m_data[ring*m_angularBins+sec]);
                update();
            }
        } else if (m_hoveredRing!=-1){ m_hoveredRing=m_hoveredSector=-1; update(); }
    } else if (m_hoveredRing!=-1){ m_hoveredRing=m_hoveredSector=-1; update(); }

    if (m_dragging){
        QPoint d = e->pos()-m_lastMousePos;
        m_panOffset += QPointF(d.x(),-d.y());
        m_lastMousePos=e->pos();
        update();
    }
}

void PolarPyWidget::mouseReleaseEvent(QMouseEvent* e)
{ if (e->button()==Qt::LeftButton) m_dragging=false; }

void PolarPyWidget::mouseDoubleClickEvent(QMouseEvent*)
{ resetView(); }
