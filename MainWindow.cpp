#include "MainWindow.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int   ANGULAR_SAMPLE_COUNT = MainController::ANGULAR_SAMPLE_COUNT;
static constexpr int   RECT_HISTORY_ROWS    = MainController::RECT_HISTORY_ROWS;
static constexpr float LOGICAL_WIDTH        = 64.0f;
static constexpr float LOGICAL_HEIGHT       = 600.0f;
static constexpr float DATA_MIN             = 0.0f;
static constexpr float DATA_MAX             = 100.0f;

// ─────────────────────────────────────────────────────────────────────────────
// MainController
// ─────────────────────────────────────────────────────────────────────────────
MainController::MainController(PolarPyWidget* polar, RasterStripWidget* rect, QObject* parent)
    : QObject(parent), m_polar(polar), m_rect(rect)
{
    m_polar->setCumulativeMode(false);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &MainController::updateData);
}

void MainController::start() { m_timer->start(); }
void MainController::stop()  { m_timer->stop();  }
bool MainController::isRunning() const { return m_timer->isActive(); }

void MainController::setInputData(float* data, int numberOfLines,
                                   float startRange, float stopRange)
{
    m_data             = data;
    m_numberOfLines    = numberOfLines;
    m_startRange       = startRange;
    m_stopRange        = stopRange;
    m_newDataAvailable = true;
}

void MainController::resetHistory()
{
    m_polarHistory.clear();
    m_polar->clearCumulativeData();
    m_rect->setDataDimensions(ANGULAR_SAMPLE_COUNT, RECT_HISTORY_ROWS);
}

void MainController::updateData()
{
    if (!m_newDataAvailable || !m_data || m_numberOfLines <= 0)
    {
        m_newDataAvailable = false;
        return;
    }

    const int newSamples = m_numberOfLines * ANGULAR_SAMPLE_COUNT;
    m_normalizedLines.resize(size_t(newSamples));
    normalizeInto(m_data, newSamples, m_startRange, m_stopRange, m_normalizedLines.data());

    m_polar->setMinRange(m_startRange);
    m_polar->setMaxRange(m_stopRange);
    m_rect->setMinRange(m_startRange);
    m_rect->setMaxRange(m_stopRange);

    pushIntoPolarHistory(m_normalizedLines.data(), m_numberOfLines);
    m_polar->plotData(m_polarHistory.data(), POLAR_HISTORY_DEPTH, ANGULAR_SAMPLE_COUNT);
    m_rect->plotData(m_normalizedLines.data(), m_numberOfLines, ANGULAR_SAMPLE_COUNT,
                     ScrollMode::PushFromTop, 0);

    m_newDataAvailable = false;
}

void MainController::normalizeInto(const float* src, int count,
                                    float startRange, float stopRange, float* dst)
{
    const float span = stopRange - startRange;
    if (std::abs(span) < 1e-9f) { std::fill(dst, dst + count, 0.5f); return; }
    for (int i = 0; i < count; ++i)
        dst[i] = std::max(0.0f, std::min(1.0f, (src[i] - startRange) / span));
}

void MainController::pushIntoPolarHistory(const float* normalizedLines, int numberOfLines)
{
    if (m_polarHistory.empty())
        m_polarHistory.assign(size_t(POLAR_HISTORY_DEPTH) * size_t(ANGULAR_SAMPLE_COUNT), 0.0f);

    const int n = std::min(numberOfLines, POLAR_HISTORY_DEPTH);
    for (int ring = POLAR_HISTORY_DEPTH - 1; ring >= n; --ring)
        std::copy(m_polarHistory.begin() + size_t(ring - n) * ANGULAR_SAMPLE_COUNT,
                  m_polarHistory.begin() + size_t(ring - n + 1) * ANGULAR_SAMPLE_COUNT,
                  m_polarHistory.begin() + size_t(ring) * ANGULAR_SAMPLE_COUNT);
    for (int i = 0; i < n; ++i)
        std::copy(normalizedLines + size_t(i) * ANGULAR_SAMPLE_COUNT,
                  normalizedLines + size_t(i + 1) * ANGULAR_SAMPLE_COUNT,
                  m_polarHistory.begin() + size_t(i) * ANGULAR_SAMPLE_COUNT);
}

// ─────────────────────────────────────────────────────────────────────────────
// SyntheticDataSource — moving pulse
// Frame N: column (N % ANGULAR_SAMPLE_COUNT) = 100, all others = 0
// ─────────────────────────────────────────────────────────────────────────────
SyntheticDataSource::SyntheticDataSource(MainController* ctrl, QObject* parent)
    : QObject(parent), m_ctrl(ctrl)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &SyntheticDataSource::generate);
}

void SyntheticDataSource::start() { m_timer->start(); }
void SyntheticDataSource::stop()  { m_timer->stop();  }
bool SyntheticDataSource::isRunning() const { return m_timer->isActive(); }

void SyntheticDataSource::generate()
{
    const int numberOfLines = 1;
    m_buffer.assign(size_t(numberOfLines) * ANGULAR_SAMPLE_COUNT, m_startRange);

    // One active pulse cell = stopRange value, all others = startRange
    const int activeCol = m_pulsePos % ANGULAR_SAMPLE_COUNT;
    m_buffer[size_t(activeCol)] = m_stopRange;

    m_pulsePos = (m_pulsePos + 1) % ANGULAR_SAMPLE_COUNT;

    m_ctrl->setInputData(m_buffer.data(), numberOfLines, m_startRange, m_stopRange);
    m_ctrl->updateData();
}

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Polar + Rectangular Live View");
    resize(1400, 700);

    m_polar = new PolarPyWidget;
    m_rect  = new RasterStripWidget;

    // Green-only colormap applied via ColorMapType::Green (defined in colormap)
    m_polar->setColorMap(ColorMapType::Green);
    m_rect->setColorMap(ColorMapType::Green);

    // Req 4: polar default -45 to +45
    m_polar->setStartAngle(-135.0f);
    m_polar->setEndAngle(-45.0f);
    m_polar->setMinRange(DATA_MIN);
    m_polar->setMaxRange(DATA_MAX);

    m_rect->setDataDimensions(ANGULAR_SAMPLE_COUNT, RECT_HISTORY_ROWS);
    m_rect->setMinRange(DATA_MIN);
    m_rect->setMaxRange(DATA_MAX);
    m_rect->setLogicalWidth(LOGICAL_WIDTH);
    m_rect->setLogicalHeight(LOGICAL_HEIGHT);

    m_ctrl       = new MainController(m_polar, m_rect, this);
    m_dataSource = new SyntheticDataSource(m_ctrl, this);

    // ── Layout ────────────────────────────────────────────────────────────
    QWidget*     central = new QWidget;
    QVBoxLayout* root    = new QVBoxLayout(central);

    QHBoxLayout* vizRow = new QHBoxLayout;
    vizRow->addWidget(m_polar, 1);
    vizRow->addWidget(m_rect,  1);
    root->addLayout(vizRow, 1);

    QHBoxLayout* ctrlRow = new QHBoxLayout;
    m_btnStartStop = new QPushButton("▶ Start");
    m_btnStartStop->setCheckable(true);
    m_btnFeedData  = new QPushButton("⚡ Feed Data");
    m_btnFeedData->setCheckable(true);
    m_btnReset     = new QPushButton("⟳ Reset");
    m_statusLbl    = new QLabel("Stopped.");

    ctrlRow->addWidget(m_btnStartStop);
    ctrlRow->addWidget(m_btnFeedData);
    ctrlRow->addWidget(m_btnReset);
    ctrlRow->addStretch();
    ctrlRow->addWidget(m_statusLbl);
    root->addLayout(ctrlRow);

    setCentralWidget(central);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_btnStartStop, &QPushButton::toggled, [this](bool on) {
        on ? m_ctrl->start() : m_ctrl->stop();
        m_btnStartStop->setText(on ? "■ Stop" : "▶ Start");
        refreshStatus();
    });

    connect(m_btnFeedData, &QPushButton::toggled, [this](bool on) {
        on ? m_dataSource->start() : m_dataSource->stop();
        m_btnFeedData->setText(on ? "⏸ Stop Feed" : "⚡ Feed Data");
        refreshStatus();
    });

    connect(m_btnReset, &QPushButton::clicked, [this]() {
        m_polar->resetView();
        m_rect->resetView();
        m_ctrl->resetHistory();
        m_statusLbl->setText("Reset.");
    });
}

void MainWindow::refreshStatus()
{
    QString s = m_ctrl->isRunning() ? "Live" : "Stopped";
    s += m_dataSource->isRunning() ? " | Feeding" : " | No feed";
    m_statusLbl->setText(s);
}
