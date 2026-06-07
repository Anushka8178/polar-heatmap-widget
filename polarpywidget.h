#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>

/**
 * PolarPyWidget
 *
 * Day 2 — Polar grid visualisation.
 *
 * Extends the Day 1 OpenGL widget foundation into a proper
 * polar coordinate widget. Draws concentric rings and radial
 * spoke lines based on configurable range and angle settings.
 *
 * Day 3+ will add: heatmap data, colour mapping, interaction.
 */
class PolarPyWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PolarPyWidget(QWidget* parent = nullptr);
    ~PolarPyWidget() override = default;

    // --- Range configuration ---
    void setMinRange(float value);
    void setMaxRange(float value);

    // --- Angle configuration ---
    void setStartAngle(float degrees);
    void setEndAngle(float degrees);

    // --- Grid density ---
    void setRadialBins(int bins);
    void setAngularBins(int bins);

    float minRange()    const { return m_minRange; }
    float maxRange()    const { return m_maxRange; }
    float startAngle()  const { return m_startAngle; }
    float endAngle()    const { return m_endAngle; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    // Draws concentric ring arcs
    void drawRings();

    // Draws radial spoke lines
    void drawSpokes();

    // Converts polar (r, theta_degrees) to Cartesian (x, y) in pixel space
    void polarToCartesian(float r, float thetaDeg, float& x, float& y) const;

    // Configuration
    float m_minRange    =   0.0f;
    float m_maxRange    = 100.0f;
    float m_startAngle  =   0.0f;
    float m_endAngle    = 360.0f;
    int   m_radialBins  =    5;
    int   m_angularBins =   12;

    // Viewport size (updated in resizeGL)
    int m_width  = 1;
    int m_height = 1;
};
