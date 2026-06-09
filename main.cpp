#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <vector>
#include <random>
#include "polarpywidget.h"

static std::vector<unsigned char> generateData(int rows, int cols)
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<unsigned char> buf(rows * cols);
    for (auto& v : buf) v = static_cast<unsigned char>(dist(rng));
    return buf;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QMainWindow win;
    win.setWindowTitle("Polar Heatmap Widget — Day 3 (Guide Review Update)");

    PolarPyWidget* w = new PolarPyWidget;
    w->setMinRange(0);
    w->setMaxRange(100);
    w->setStartAngle(0);
    w->setEndAngle(360);   // try 180 or 90 to test dynamic layout

    auto buf = generateData(8, 16);
    w->plotData(reinterpret_cast<char*>(buf.data()), 8, 16);

    win.setCentralWidget(w);
    win.statusBar()->showMessage(
        "OpenGL 3.3 Core | VBO rendering | Range labels | Dynamic layout");
    win.resize(900, 700);
    win.show();
    return a.exec();
}
