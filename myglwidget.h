#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>

/**
 * MyGLWidget
 *
 * Day 1 — Base OpenGL widget for the Polar Heatmap project.
 *
 * Establishes the rendering context, viewport management,
 * and OpenGL initialization. This class will be extended
 * into PolarPyWidget from Day 2 onward.
 */
class MyGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MyGLWidget(QWidget* parent = nullptr);
    ~MyGLWidget() override = default;

protected:
    /** One-time OpenGL setup: clear colour, blending. */
    void initializeGL() override;

    /** Called on every resize: update viewport + projection. */
    void resizeGL(int width, int height) override;

    /** Called every frame: clear the canvas (polar sectors drawn from Day 2). */
    void paintGL() override;
};
