#include "polarpywidget.h"

#include <cmath>
#include <QPainter>
#include <QFont>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float degToRad(float d)
{
    return d * static_cast<float>(M_PI) / 180.0f;
}

// ----------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------
PolarPyWidget::PolarPyWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
}

// ----------------------------------------------------------------
// Configuration setters — each triggers a redraw
// ----------------------------------------------------------------
void PolarPyWidget::setMinRange(float value)   { m_minRange    = value;   update(); }
void PolarPyWidget::setMaxRange(float value)   { m_maxRange    = value;   update(); }
void PolarPyWidget::setStartAngle(float deg)   { m_startAngle  = deg;     update(); }
void PolarPyWidget::setEndAngle(float deg)     { m_endAngle    = deg;     update(); }
void PolarPyWidget::setRadialBins(int bins)    { m_radialBins  = bins;    update(); }
void PolarPyWidget::setAngularBins(int bins)   { m_angularBins = bins;    update(); }

// ----------------------------------------------------------------
// initializeGL
// ----------------------------------------------------------------
void PolarPyWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // Dark background for polar display
    glClearColor(0.07f, 0.07f, 0.12f, 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

// ----------------------------------------------------------------
// resizeGL
// ----------------------------------------------------------------
void PolarPyWidget::resizeGL(int width, int height)
{
    m_width  = width;
    m_height = height;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Centre the origin; x in [-hw, hw], y in [-hh, hh]
    float hw = width  * 0.5f;
    float hh = height * 0.5f;
    glOrtho(-hw, hw, -hh, hh, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ----------------------------------------------------------------
// paintGL
// ----------------------------------------------------------------
void PolarPyWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    drawRings();
    drawSpokes();

    // Draw range labels using QPainter overlay
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(QFont("Monospace", 9));
    p.setPen(QColor(140, 160, 200));

    float halfSide = std::min(m_width, m_height) * 0.5f - 20.0f;
    float scale    = halfSide / m_maxRange;
    float cx       = m_width  * 0.5f;
    float cy       = m_height * 0.5f;

    for (int r = 0; r <= m_radialBins; ++r) {
        float rangeVal = m_minRange + (m_maxRange - m_minRange) * float(r) / m_radialBins;
        float px       = rangeVal * scale;
        // Label at the 0-degree direction
        float ang = degToRad(m_startAngle);
        float lx  = cx + px * std::cos(ang) + 4;
        float ly  = cy - px * std::sin(ang) - 4;   // y-flip for screen coords
        p.drawText(QPointF(lx, ly), QString::number(int(rangeVal)));
    }

    // Widget title
    p.setPen(QColor(80, 100, 160, 160));
    p.setFont(QFont("Monospace", 8));
    p.drawText(QRect(4, m_height - 18, 300, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString("PolarPyWidget  rings=%1  spokes=%2")
                   .arg(m_radialBins).arg(m_angularBins));
}

// ----------------------------------------------------------------
// polarToCartesian
//
// Converts (r, thetaDeg) in data-space to (x, y) in screen pixels.
// Origin is at widget centre; y increases upward (standard maths).
// ----------------------------------------------------------------
void PolarPyWidget::polarToCartesian(float r, float thetaDeg,
                                     float& x, float& y) const
{
    float halfSide = std::min(m_width, m_height) * 0.5f - 20.0f;
    float scale    = halfSide / m_maxRange;

    float rad = degToRad(thetaDeg);
    x = r * scale * std::cos(rad);
    y = r * scale * std::sin(rad);
}

// ----------------------------------------------------------------
// drawRings — concentric arcs at each radial boundary
// ----------------------------------------------------------------
void PolarPyWidget::drawRings()
{
    float rangeSpan = m_maxRange - m_minRange;
    float halfSide  = std::min(m_width, m_height) * 0.5f - 20.0f;
    float scale     = halfSide / m_maxRange;

    const int arcSteps = 180;

    for (int r = 0; r <= m_radialBins; ++r) {
        float rangeVal = m_minRange + rangeSpan * float(r) / m_radialBins;
        float radius   = rangeVal * scale;

        // Outermost ring is brighter
        bool isOuter = (r == m_radialBins);
        glLineWidth(isOuter ? 1.6f : 0.9f);
        glColor4f(0.35f, 0.45f, 0.75f, isOuter ? 0.8f : 0.4f);

        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= arcSteps; ++i) {
            float t   = float(i) / arcSteps;
            float ang = degToRad(m_startAngle + (m_endAngle - m_startAngle) * t);
            glVertex2f(radius * std::cos(ang), radius * std::sin(ang));
        }
        glEnd();
    }
}

// ----------------------------------------------------------------
// drawSpokes — radial lines from inner ring to outer ring
// ----------------------------------------------------------------
void PolarPyWidget::drawSpokes()
{
    float halfSide  = std::min(m_width, m_height) * 0.5f - 20.0f;
    float scale     = halfSide / m_maxRange;
    float innerR    = m_minRange * scale;
    float outerR    = m_maxRange * scale;

    glLineWidth(0.8f);

    for (int s = 0; s <= m_angularBins; ++s) {
        float ang = degToRad(m_startAngle +
                    (m_endAngle - m_startAngle) * float(s) / m_angularBins);
        float ca  = std::cos(ang);
        float sa  = std::sin(ang);

        // Fade from transparent at centre to visible at edge
        glBegin(GL_LINES);
            glColor4f(0.3f, 0.4f, 0.7f, 0.15f);
            glVertex2f(innerR * ca, innerR * sa);
            glColor4f(0.4f, 0.55f, 0.85f, 0.55f);
            glVertex2f(outerR * ca, outerR * sa);
        glEnd();
    }
}
