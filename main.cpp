#include <QApplication>
#include "myglwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MyGLWidget w;
    w.setWindowTitle("Polar Heatmap Widget — Day 1");
    w.resize(800, 600);
    w.show();

    return a.exec();
}
