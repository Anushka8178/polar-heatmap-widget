#include <QApplication>
#include <QMainWindow>
#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <vector>
#include <random>
#include <cmath>
#include "polarpywidget.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Preset data generators ──────────────────────────────────
static std::vector<unsigned char> makeRandom(int r,int c)
{
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> d(0,255);
    std::vector<unsigned char> b(r*c);
    for(auto& v:b) v=static_cast<unsigned char>(d(rng));
    return b;
}
static std::vector<unsigned char> makeHotspot(int rows,int cols)
{
    std::vector<unsigned char> b(rows*cols, 10);
    auto blob=[&](float cr,float cs,float str){
        for(int r=0;r<rows;++r) for(int s=0;s<cols;++s){
            float dr=float(r)-cr*rows, ds=float(s)-cs*cols;
            int v=b[r*cols+s]+int(str*std::exp(-(dr*dr+ds*ds)/(0.08f*rows*rows)));
            b[r*cols+s]=static_cast<unsigned char>(std::min(255,v));
        }};
    blob(0.6f,0.2f,250); blob(0.4f,0.7f,200);
    return b;
}
static std::vector<unsigned char> makeSpiral(int r,int c)
{
    std::vector<unsigned char> b(r*c);
    for(int i=0;i<r;++i) for(int j=0;j<c;++j)
        b[i*c+j]=static_cast<unsigned char>(float(i*c+j)/float(r*c-1)*255);
    return b;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("Fusion");

    QMainWindow win;
    win.setWindowTitle("Polar Heatmap Widget — Final Release (OpenGL 3.3 Core)");

    PolarPyWidget* w = new PolarPyWidget;
    win.setCentralWidget(w);

    // ── Control panel ────────────────────────────────────
    QDockWidget* dock  = new QDockWidget("Controls", &win);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    QWidget*     panel = new QWidget;
    QVBoxLayout* panL  = new QVBoxLayout(panel);
    panL->setSpacing(8);
    dock->setWidget(panel);
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setFixedWidth(215);

    auto grp=[](const QString& t){ auto* g=new QGroupBox(t); return g; };
    auto dsp=[](double lo,double hi,double v){ auto* s=new QDoubleSpinBox; s->setRange(lo,hi); s->setValue(v); s->setDecimals(1); return s; };
    auto isp=[](int lo,int hi,int v){ auto* s=new QSpinBox; s->setRange(lo,hi); s->setValue(v); return s; };
    auto btn=[](const QString& t){ return new QPushButton(t); };

    QGroupBox* gbP=grp("Data Preset");
    QVBoxLayout* pL=new QVBoxLayout(gbP);
    QComboBox* combo=new QComboBox;
    combo->addItems({"Random","Hotspots","Spiral"});
    pL->addWidget(combo);
    panL->addWidget(gbP);

    QGroupBox* gbG=grp("Grid");
    QFormLayout* gF=new QFormLayout(gbG);
    auto* rSpin=isp(1,32,8); auto* sSpin=isp(1,64,16);
    gF->addRow("Rings:",sSpin->parentWidget()); // placeholder
    gF->addRow("Rings:",rSpin); gF->addRow("Sectors:",sSpin);
    gF->removeRow(0);
    panL->addWidget(gbG);

    QGroupBox* gbR=grp("Radius");
    QFormLayout* rF=new QFormLayout(gbR);
    auto* minS=dsp(0,999,0); auto* maxS=dsp(1,999,100);
    rF->addRow("Min:",minS); rF->addRow("Max:",maxS);
    panL->addWidget(gbR);

    QGroupBox* gbA=grp("Angles");
    QFormLayout* aF=new QFormLayout(gbA);
    auto* stS=dsp(-360,720,0); auto* enS=dsp(-360,720,360);
    aF->addRow("Start °:",stS); aF->addRow("End °:",enS);
    panL->addWidget(gbA);

    auto* bApply=btn("▶  Apply / Render");
    auto* bReset=btn("⟳  Reset View");
    auto* bAnim =btn("⚡  Live Animate");
    bAnim->setCheckable(true);
    panL->addWidget(bApply); panL->addWidget(bReset); panL->addWidget(bAnim);
    panL->addStretch();

    QLabel* tip=new QLabel("Scroll=zoom  Drag=pan\nDbl-click=reset  Click=select");
    tip->setAlignment(Qt::AlignCenter);
    tip->setStyleSheet("color:gray;font-size:9px;");
    panL->addWidget(tip);

    // ── State ────────────────────────────────────────────
    std::vector<unsigned char> buf = makeHotspot(8,16);
    w->setMinRange(0); w->setMaxRange(100);
    w->setStartAngle(0); w->setEndAngle(360);
    w->plotData(reinterpret_cast<char*>(buf.data()),8,16);

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> noise(-20,20);

    auto doApply=[&](){
        w->setMinRange(float(minS->value())); w->setMaxRange(float(maxS->value()));
        w->setStartAngle(float(stS->value())); w->setEndAngle(float(enS->value()));
        int ro=rSpin->value(), co=sSpin->value();
        switch(combo->currentIndex()){
            case 1: buf=makeHotspot(ro,co); break;
            case 2: buf=makeSpiral(ro,co);  break;
            default: buf=makeRandom(ro,co); break;
        }
        w->plotData(reinterpret_cast<char*>(buf.data()),ro,co);
    };

    QObject::connect(bApply,&QPushButton::clicked, doApply);
    QObject::connect(bReset,&QPushButton::clicked, w, &PolarPyWidget::resetView);

    QTimer* timer=new QTimer(&win);
    QObject::connect(timer,&QTimer::timeout,[&](){
        for(auto& v:buf){ int nv=int(v)+noise(rng); v=static_cast<unsigned char>(std::max(0,std::min(255,nv))); }
        w->plotData(reinterpret_cast<char*>(buf.data()),rSpin->value(),sSpin->value());
    });
    QObject::connect(bAnim,&QPushButton::toggled,[&](bool on){
        bAnim->setText(on?"■  Stop":"⚡  Live Animate");
        on?timer->start(150):timer->stop();
    });

    win.statusBar()->showMessage("Scroll=zoom  Drag=pan  Dbl-click=reset  Click sector=select");
    QObject::connect(w,&PolarPyWidget::sectorSelected,[&win](int ri,int se,int va){
        win.statusBar()->showMessage(QString("Selected — Ring:%1  Sector:%2  Value:%3").arg(ri).arg(se).arg(va));
    });

    win.resize(1050,720);
    win.show();
    return a.exec();
}
