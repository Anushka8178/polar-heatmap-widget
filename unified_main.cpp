// ─────────────────────────────────────────────────────────────────────────────
// unified_main.cpp  (FIXED)
//
// Fixes applied:
//  1. SyntheticDataSource::generate() now calls ctrl->updateData() immediately
//     after setInputData() so data is consumed on the same tick it's produced,
//     instead of waiting for the controller's independent 100ms timer which
//     could fire before or after the data source — causing missed/stale frames.
//  2. MainController's own QTimer is kept only as a safety fallback; the
//     primary render path is now: generate() → setInputData() → updateData().
// ─────────────────────────────────────────────────────────────────────────────
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QLabel>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>

#include "polarpywidget.h"
#include "../Rectangular/files/rasterstripwidget.h"

static constexpr int RECT_HISTORY_ROWS = 50;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int RECT_LOGICAL_WIDTH  = 1000;
static constexpr int RECT_LOGICAL_HEIGHT = 600;

static constexpr int POLAR_HISTORY_DEPTH  = 100;
static constexpr int ANGULAR_SAMPLE_COUNT = 64;

// ─────────────────────────────────────────────────────────────────────────────
// MainController
// ─────────────────────────────────────────────────────────────────────────────
class MainController : public QObject
{
    Q_OBJECT
public:
    MainController(PolarPyWidget* polar, RasterStripWidget* rect, QObject* parent = nullptr)
        : QObject(parent), m_polar(polar), m_rect(rect)
    {
        m_polar->setCumulativeMode(false);

        // Timer kept as fallback; primary path is direct updateData() call
        // from SyntheticDataSource after setInputData().
        m_timer = new QTimer(this);
        m_timer->setInterval(100);
        connect(m_timer, &QTimer::timeout, this, &MainController::updateData);
    }

    void start() { m_timer->start(); }
    void stop()  { m_timer->stop();  }
    bool isRunning() const { return m_timer->isActive(); }

    void setInputData(float* data, int numberOfLines, float startRange, float stopRange)
    {
        m_data             = data;
        m_numberOfLines    = numberOfLines;
        m_startRange       = startRange;
        m_stopRange        = stopRange;
        m_newDataAvailable = true;
    }

    void resetHistory()
    {
        m_polarHistory.clear();
        m_polar->clearCumulativeData();
        m_rect->setDataDimensions(ANGULAR_SAMPLE_COUNT, RECT_HISTORY_ROWS);
    }

    long long totalLinesIngested() const { return m_totalLinesIngested; }

public slots:
    void updateData()
    {
        if (!m_newDataAvailable)
            return;

        if (!m_data || m_numberOfLines <= 0)
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

        m_totalLinesIngested += m_numberOfLines;
        m_newDataAvailable = false;
    }

private:
    static void normalizeInto(const float* src, int count,
                               float startRange, float stopRange, float* dst)
    {
        const float span = stopRange - startRange;
        if (std::abs(span) < 1e-9f)
        {
            std::fill(dst, dst + count, 0.5f);
            return;
        }
        for (int i = 0; i < count; ++i)
        {
            const float t = (src[i] - startRange) / span;
            dst[i] = std::max(0.0f, std::min(1.0f, t));
        }
    }

    void pushIntoPolarHistory(const float* normalizedNewLines, int numberOfLines)
    {
        if (m_polarHistory.empty())
            m_polarHistory.assign(size_t(POLAR_HISTORY_DEPTH) * size_t(ANGULAR_SAMPLE_COUNT), 0.0f);

        const int n = std::min(numberOfLines, POLAR_HISTORY_DEPTH);

        for (int ring = POLAR_HISTORY_DEPTH - 1; ring >= n; --ring)
        {
            std::copy(
                m_polarHistory.begin() + size_t(ring - n) * ANGULAR_SAMPLE_COUNT,
                m_polarHistory.begin() + size_t(ring - n + 1) * ANGULAR_SAMPLE_COUNT,
                m_polarHistory.begin() + size_t(ring) * ANGULAR_SAMPLE_COUNT);
        }
        for (int i = 0; i < n; ++i)
        {
            std::copy(
                normalizedNewLines + size_t(i) * ANGULAR_SAMPLE_COUNT,
                normalizedNewLines + size_t(i + 1) * ANGULAR_SAMPLE_COUNT,
                m_polarHistory.begin() + size_t(i) * ANGULAR_SAMPLE_COUNT);
        }
    }

    PolarPyWidget*      m_polar;
    RasterStripWidget*  m_rect;
    QTimer*             m_timer = nullptr;

    float* m_data              = nullptr;
    int    m_numberOfLines     = 0;
    float  m_startRange        = 0.0f;
    float  m_stopRange         = 0.0f;
    bool   m_newDataAvailable  = false;
    long long m_totalLinesIngested = 0;

    std::vector<float> m_normalizedLines;
    std::vector<float> m_polarHistory;
};

// ─────────────────────────────────────────────────────────────────────────────
// SyntheticDataSource
//
// FIX: after setInputData(), calls ctrl->updateData() directly so data is
// consumed on the same tick it's produced. No more race between two
// independent 100ms timers that could cause the controller to poll before
// new data arrived (rendering stale/empty frames).
// ─────────────────────────────────────────────────────────────────────────────
class SyntheticDataSource : public QObject
{
    Q_OBJECT
public:
    explicit SyntheticDataSource(MainController* ctrl, QObject* parent = nullptr)
        : QObject(parent), m_ctrl(ctrl)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(100);
        connect(m_timer, &QTimer::timeout, this, &SyntheticDataSource::generate);
    }

    void start() { m_timer->start(); }
    void stop()  { m_timer->stop();  }
    bool isRunning() const { return m_timer->isActive(); }

public slots:
    void generate()
    {
        const int numberOfLines = 1;
        m_buffer.resize(size_t(numberOfLines) * ANGULAR_SAMPLE_COUNT);

        for (int s = 0; s < ANGULAR_SAMPLE_COUNT; ++s)
        {
            const float angleFrac = float(s) / float(ANGULAR_SAMPLE_COUNT);
            const float wave = 0.5f + 0.45f * std::sin(2.0f * float(M_PI) * angleFrac * 3.0f
                                                        + m_phase);
            const float noise = m_noiseDist(m_rng);
            float v = wave * 80.0f + noise;
            v = std::max(m_startRange, std::min(m_stopRange, v));
            m_buffer[size_t(s)] = v;
        }

        m_phase += 0.18f;
        if (m_phase > 2.0f * float(M_PI)) m_phase -= 2.0f * float(M_PI);

        // FIX: push data then immediately consume it — no timer race
        m_ctrl->setInputData(m_buffer.data(), numberOfLines, m_startRange, m_stopRange);
        m_ctrl->updateData();
    }

private:
    MainController* m_ctrl;
    QTimer* m_timer = nullptr;
    std::vector<float> m_buffer;
    float m_phase = 0.0f;
    float m_startRange = 0.0f;
    float m_stopRange  = 100.0f;

    std::mt19937 m_rng{std::random_device{}()};
    std::normal_distribution<float> m_noiseDist{0.0f, 6.0f};
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

// Static buffers for the test data button (defined at file scope so the
// lambda can capture them safely across timer ticks)
static std::vector<float> testBuffer;
static int testPhase = 0;
static constexpr int TEST_LINES = 5;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    PolarPyWidget* polarWidget = new PolarPyWidget;
    RasterStripWidget* rectWidget = new RasterStripWidget;

    polarWidget->setColorMap(ColorMapType::Heat);
    rectWidget->setColorMap(ColorMapType::Viridis);

    polarWidget->setMinRange(0.0f);
    polarWidget->setMaxRange(100.0f);
    polarWidget->setStartAngle(0.0f);
    polarWidget->setEndAngle(360.0f);

    rectWidget->setDataDimensions(ANGULAR_SAMPLE_COUNT, RECT_HISTORY_ROWS);
    rectWidget->setMinRange(0.0f);
    rectWidget->setMaxRange(100.0f);
    rectWidget->setLogicalWidth(float(RECT_LOGICAL_WIDTH));
    rectWidget->setLogicalHeight(float(RECT_LOGICAL_HEIGHT));

    MainController* ctrl = new MainController(polarWidget, rectWidget);
    SyntheticDataSource* dataSource = new SyntheticDataSource(ctrl);

    QWidget* central = new QWidget;
    QVBoxLayout* root = new QVBoxLayout(central);

    QHBoxLayout* vizRow = new QHBoxLayout;
    vizRow->addWidget(polarWidget, 1);
    vizRow->addWidget(rectWidget, 1);
    root->addLayout(vizRow, 1);

    QHBoxLayout* ctrlRow = new QHBoxLayout;

    QPushButton* btnStartStop = new QPushButton("▶ Start");
    btnStartStop->setCheckable(true);

    QPushButton* btnFeedData = new QPushButton("⚡ Feed Data");
    btnFeedData->setCheckable(true);

    QPushButton* btnReset = new QPushButton("⟳ Reset");

    QPushButton* btnTestData = new QPushButton("🧪 Generate Test Data");
    btnTestData->setCheckable(true);

    QLabel* statusLbl = new QLabel("Timer stopped. No data feed.");

    ctrlRow->addWidget(btnStartStop);
    ctrlRow->addWidget(btnFeedData);
    ctrlRow->addWidget(btnReset);
    ctrlRow->addWidget(btnTestData);
    ctrlRow->addStretch();
    ctrlRow->addWidget(statusLbl);
    root->addLayout(ctrlRow);

    QMainWindow win;
    win.setWindowTitle("Polar + Rectangular Live View");
    win.setCentralWidget(central);
    win.resize(1400, 700);

    auto refreshStatus = [&]()
    {
        QString s = ctrl->isRunning() ? "Live" : "Timer stopped";
        s += dataSource->isRunning() ? " | Feeding data" : " | No data feed";
        statusLbl->setText(s);
    };

    QObject::connect(btnStartStop, &QPushButton::toggled, [&](bool on)
    {
        if (on)
        {
            ctrl->start();
            btnStartStop->setText("■ Stop");
        }
        else
        {
            ctrl->stop();
            btnStartStop->setText("▶ Start");
        }
        refreshStatus();
    });

    QObject::connect(btnFeedData, &QPushButton::toggled, [&](bool on)
    {
        if (on)
        {
            dataSource->start();
            btnFeedData->setText("⏸ Stop Feed");
        }
        else
        {
            dataSource->stop();
            btnFeedData->setText("⚡ Feed Data");
        }
        refreshStatus();
    });

    QObject::connect(btnReset, &QPushButton::clicked, [&]()
    {
        polarWidget->resetView();
        rectWidget->resetView();
        ctrl->resetHistory();
        statusLbl->setText("View and history reset.");
    });

    QTimer* testTimer = new QTimer(&win);
    testTimer->setInterval(100);

    // FIX: test data timer also calls updateData() immediately after
    // setInputData() — same pattern as SyntheticDataSource
    QObject::connect(testTimer, &QTimer::timeout, [ctrl]()
    {
        testBuffer.assign(size_t(TEST_LINES) * size_t(ANGULAR_SAMPLE_COUNT), 0.0f);
        for (int line = 0; line < TEST_LINES; ++line)
        {
            for (int s = 0; s < ANGULAR_SAMPLE_COUNT; ++s)
            {
                const float angle = float(s) / float(ANGULAR_SAMPLE_COUNT) * 2.0f * float(M_PI);
                const float v = 0.5f + 0.5f * std::sin(angle * 3.0f + float(testPhase) * 0.15f);
                testBuffer[size_t(line) * ANGULAR_SAMPLE_COUNT + s] = v;
            }
        }
        ++testPhase;
        ctrl->setInputData(testBuffer.data(), TEST_LINES, 0.0f, 1.0f);
        ctrl->updateData(); // FIX: consume immediately
    });

    QObject::connect(btnTestData, &QPushButton::toggled, [&](bool on)
    {
        if (on)
        {
            if (!ctrl->isRunning())
            {
                ctrl->start();
                btnStartStop->setChecked(true);
            }
            testTimer->start();
            btnTestData->setText("⏹ Stop Test Data");
        }
        else
        {
            testTimer->stop();
            btnTestData->setText("🧪 Generate Test Data");
        }
    });

    win.show();
    return app.exec();
}

#include "unified_main.moc"
