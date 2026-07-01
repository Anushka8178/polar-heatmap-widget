#pragma once

#include <QMainWindow>
#include <QTimer>
#include <vector>
#include <cmath>

#include "include/polarpywidget.h"
#include "include/rasterstripwidget.h"

class QPushButton;
class QLabel;

// ─────────────────────────────────────────────────────────────────────────────
// MainController
// Owns no data generation. Receives data via setInputData() and pushes
// it to both widgets on each timer tick (or immediately via updateData()).
// ─────────────────────────────────────────────────────────────────────────────
class MainController : public QObject
{
    Q_OBJECT
public:
    static constexpr int   POLAR_HISTORY_DEPTH  = 100;
    static constexpr int   ANGULAR_SAMPLE_COUNT = 64;
    static constexpr int   RECT_HISTORY_ROWS    = 200;

    MainController(PolarPyWidget* polar, RasterStripWidget* rect, RasterStripWidget* rectB, QObject* parent = nullptr);

    void start();
    void stop();
    bool isRunning() const;

    void setInputData(float* data, int numberOfLines, float startRange, float stopRange);
    void resetHistory();

public slots:
    void updateData();

private:
    static void normalizeInto(const float* src, int count,
                               float startRange, float stopRange, float* dst);

    PolarPyWidget*     m_polar;
    int                 m_sweepRing = 0;
    bool                m_sweepInitialized = false;
    RasterStripWidget* m_rect;
    RasterStripWidget* m_rectB;
    QTimer*            m_timer = nullptr;

    float* m_data             = nullptr;
    int    m_numberOfLines    = 0;
    float  m_startRange       = 0.0f;
    float  m_stopRange        = 100.0f;
    bool   m_newDataAvailable = false;

    std::vector<float> m_normalizedLines;
    std::vector<float> m_polarHistory;
};

// ─────────────────────────────────────────────────────────────────────────────
// SyntheticDataSource
// Produces a single moving-pulse line: one active cell = 100, rest = 0.
// The pulse moves one column right per tick, wrapping around.
// ─────────────────────────────────────────────────────────────────────────────
class SyntheticDataSource : public QObject
{
    Q_OBJECT
public:
    explicit SyntheticDataSource(MainController* ctrl, QObject* parent = nullptr);
    void start();
    void stop();
    bool isRunning() const;

public slots:
    void generate();

private:
    MainController*    m_ctrl;
    QTimer*            m_timer   = nullptr;
    std::vector<float> m_buffer;
    int                m_pulsePos = 0;
    float              m_startRange = 0.0f;
    float              m_stopRange  = 100.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    PolarPyWidget*      m_polar      = nullptr;
    RasterStripWidget*  m_rect       = nullptr;
    RasterStripWidget*  m_rectB      = nullptr;
    MainController*     m_ctrl       = nullptr;
    SyntheticDataSource* m_dataSource = nullptr;

    QPushButton* m_btnStartStop = nullptr;
    QPushButton* m_btnFeedData  = nullptr;
    QPushButton* m_btnReset     = nullptr;
    QLabel*      m_statusLbl    = nullptr;

    void refreshStatus();
};
