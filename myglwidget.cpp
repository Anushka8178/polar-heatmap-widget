#include "myglwidget.h"

MyGLWidget::MyGLWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
}

// Called once when the OpenGL context is created.
void MyGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // Dark background suited for a heatmap display
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

    // Enable alpha blending — needed for heatmap colour layers
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Smooth lines for polar grid rendering (Day 2+)
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

// Called whenever the widget is resized.
void MyGLWidget::resizeGL(int width, int height)
{
    // Keep viewport in sync with widget size
    glViewport(0, 0, width, height);

    // Orthographic projection in pixel coordinates (y-down)
    // This will be the base projection for all polar rendering
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// Called every frame.
void MyGLWidget::paintGL()
{
    // Clear to background colour
    // Polar heatmap sectors will be drawn here from Day 2 onward
    glClear(GL_COLOR_BUFFER_BIT);
}
