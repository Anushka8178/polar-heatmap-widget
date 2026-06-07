#include <QApplication>
#include "polarpywidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    PolarPyWidget w;

    // Configure range
    w.setMinRange(0);
    w.setMaxRange(100);

    // Configure angle (full circle)
    w.setStartAngle(0);
    w.setEndAngle(360);

    // Grid density
    w.setRadialBins(5);
    w.setAngularBins(12);

    w.setWindowTitle("Polar Heatmap Widget — Day 2");
    w.resize(800, 600);
    w.show();

    return a.exec();
}
