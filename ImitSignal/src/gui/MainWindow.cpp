#include "gui/MainWindow.h"
#include "ui_MainWindow.h"

#include "dsp/Filters.h"
#include "dsp/SignalGenerator.h"
#include "qcustomplot.h"

#include <QElapsedTimer>
#include <QStringList>
#include <QVector>

#include <stdexcept>
#include <vector>

namespace {

QVector<double> toQVector(const std::vector<double>& v)
{
    return QVector<double>(v.begin(), v.end());
}

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);

    setupPlot(ui->plotSignal, tr("Исходный сигнал"));
    setupPlot(ui->plotFiltered, tr("После фильтрации"));

    m_signalGraph = makeGraph(ui->plotSignal, QColor(40, 110, 200), tr("Сигнал"));
    // Исходный сигнал добавляется первым, поэтому рисуется под результатом.
    m_referenceGraph = makeGraph(ui->plotFiltered, QColor(190, 190, 190), tr("Исходный"));
    m_filteredGraph = makeGraph(ui->plotFiltered, QColor(220, 80, 40), tr("После фильтра"));

    ui->plotFiltered->legend->setVisible(true);
    ui->plotFiltered->legend->setBrush(QColor(255, 255, 255, 200));

    // Масштаб и сдвиг по времени у двух графиков синхронны.
    linkXAxis(ui->plotSignal, ui->plotFiltered);
    linkXAxis(ui->plotFiltered, ui->plotSignal);

    ui->filterTypeCombo->setCurrentIndex(static_cast<int>(FilterType::LowPassFir));
    onFilterTypeChanged(ui->filterTypeCombo->currentIndex());

    connect(ui->buildButton, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(ui->filterTypeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onFilterTypeChanged);

    onBuildClicked();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupPlot(QCustomPlot* plot, const QString& title)
{
    plot->xAxis->setLabel(tr("Время, с"));
    plot->yAxis->setLabel(tr("Амплитуда"));

    QFont titleFont = font();
    titleFont.setBold(true);
    plot->plotLayout()->insertRow(0);
    plot->plotLayout()->addElement(0, 0, new QCPTextElement(plot, title, titleFont));

    // Перетаскивание и масштабирование колесом мыши.
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}

QCPGraph* MainWindow::makeGraph(QCustomPlot* plot, const QColor& color, const QString& name)
{
    QCPGraph* graph = plot->addGraph();
    graph->setPen(QPen(color));
    graph->setName(name);
    return graph;
}

void MainWindow::linkXAxis(QCustomPlot* source, QCustomPlot* target)
{
    // setRange с тем же диапазоном ничего не делает, поэтому двусторонняя связь не зацикливается.
    connect(source->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), target,
            [target](const QCPRange& range) {
                target->xAxis->setRange(range);
                target->replot(QCustomPlot::rpQueuedReplot);
            });
}

void MainWindow::onFilterTypeChanged(int index)
{
    const auto type = static_cast<FilterType>(index);
    const bool hasWindow = type != FilterType::None;
    const bool hasCutoff = type == FilterType::LowPassFir;

    ui->windowSizeLabel->setEnabled(hasWindow);
    ui->windowSizeSpin->setEnabled(hasWindow);
    ui->cutoffLabel->setEnabled(hasCutoff);
    ui->cutoffSpin->setEnabled(hasCutoff);
}

void MainWindow::onBuildClicked()
{
    const double sampleRate = ui->sampleRateSpin->value();
    const double nyquist = sampleRate / 2.0;
    const auto sampleCount = static_cast<std::size_t>(ui->sampleCountSpin->value());
    const dsp::SineParams mainSine{ui->frequencySpin->value(), ui->amplitudeSpin->value()};
    const dsp::SineParams interference{ui->interferenceFrequencySpin->value(),
                                       ui->interferenceAmplitudeSpin->value()};
    const double noiseStdDev = ui->noiseSpin->value();

    const auto filterType = static_cast<FilterType>(ui->filterTypeCombo->currentIndex());
    const double cutoff = ui->cutoffSpin->value();

    // Длина окна должна быть нечётной, чтобы у окна был центральный отсчёт.
    int windowSize = ui->windowSizeSpin->value();
    if (windowSize % 2 == 0) {
        ++windowSize;
        ui->windowSizeSpin->setValue(windowSize);
    }

    // --- Генерация ------------------------------------------------------------
    dsp::SignalGenerator generator(sampleRate);

    QElapsedTimer timer;
    timer.start();

    auto signal = generator.sine(mainSine, sampleCount);
    if (interference.amplitude > 0.0) {
        generator.addSine(signal, interference);
    }
    generator.addWhiteNoise(signal, noiseStdDev);

    const double generationMs = elapsedMs(timer);

    const QVector<double> time = toQVector(generator.timeAxis(sampleCount));
    const QVector<double> signalData = toQVector(signal);
    m_signalGraph->setData(time, signalData, true);
    m_referenceGraph->setData(time, signalData, true);

    QStringList status;
    status << tr("Генерация: %1 мс").arg(generationMs, 0, 'f', 3);

    // --- Фильтрация -----------------------------------------------------------
    if (filterType == FilterType::LowPassFir && cutoff >= nyquist) {
        m_filteredGraph->data()->clear();
        status << tr("Частота среза должна быть меньше %1 Гц (половины fs)").arg(nyquist);
    } else {
        try {
            std::vector<double> filtered;
            timer.restart();

            switch (filterType) {
            case FilterType::None:
                filtered = signal;
                break;
            case FilterType::MovingAverage:
                filtered = dsp::movingAverage(signal, static_cast<std::size_t>(windowSize));
                break;
            case FilterType::LowPassFir: {
                const auto taps = dsp::designLowPass(cutoff, sampleRate, static_cast<std::size_t>(windowSize));
                filtered = dsp::convolveSame(signal, taps);
                break;
            }
            }

            const double filterMs = elapsedMs(timer);
            m_filteredGraph->setData(time, toQVector(filtered), true);

            if (filterType != FilterType::None) {
                status << tr("Фильтрация: %1 мс (окно %2 отсч.)").arg(filterMs, 0, 'f', 3).arg(windowSize);
            }
        } catch (const std::exception& e) {
            m_filteredGraph->data()->clear();
            status << tr("Ошибка фильтра: %1").arg(QString::fromUtf8(e.what()));
        }
    }

    // --- Предупреждения -------------------------------------------------------
    if (mainSine.frequencyHz > nyquist
        || (interference.amplitude > 0.0 && interference.frequencyHz > nyquist)) {
        status << tr("Внимание: частота выше Найквиста (%1 Гц), будет наложение спектров").arg(nyquist);
    }

    ui->plotSignal->rescaleAxes();
    ui->plotFiltered->rescaleAxes();
    ui->plotSignal->replot();
    ui->plotFiltered->replot();

    statusBar()->showMessage(status.join(QStringLiteral("  |  ")));
}
