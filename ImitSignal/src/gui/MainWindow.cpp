#include "gui/MainWindow.h"
#include "ui_MainWindow.h"

#include "dsp/SignalGenerator.h"
#include "qcustomplot.h"

#include <QElapsedTimer>
#include <QVector>

namespace {

QVector<double> toQVector(const std::vector<double>& v)
{
    return QVector<double>(v.begin(), v.end());
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);

    setupPlot(ui->plotSignal, tr("Исходный сигнал"));
    setupPlot(ui->plotFiltered, tr("Отфильтрованный сигнал"));

    connect(ui->buildButton, &QPushButton::clicked, this, &MainWindow::onBuildClicked);

    // Сразу показываем сигнал с параметрами по умолчанию.
    onBuildClicked();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupPlot(QCustomPlot* plot, const QString& title)
{
    plot->addGraph();
    plot->graph(0)->setPen(QPen(QColor(40, 110, 200)));

    plot->xAxis->setLabel(tr("Время, с"));
    plot->yAxis->setLabel(tr("Амплитуда"));

    QFont titleFont = font();
    titleFont.setBold(true);
    plot->plotLayout()->insertRow(0);
    plot->plotLayout()->addElement(0, 0, new QCPTextElement(plot, title, titleFont));

    // Перетаскивание и масштабирование колесом мыши.
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}

void MainWindow::showSignal(QCustomPlot* plot,
                            const std::vector<double>& time,
                            const std::vector<double>& values)
{
    // true — ключи (время) уже отсортированы, QCustomPlot не будет их сортировать.
    plot->graph(0)->setData(toQVector(time), toQVector(values), true);
    plot->rescaleAxes();
    plot->replot();
}

void MainWindow::onBuildClicked()
{
    const double sampleRate = ui->sampleRateSpin->value();
    const double frequency = ui->frequencySpin->value();
    const double amplitude = ui->amplitudeSpin->value();
    const auto sampleCount = static_cast<std::size_t>(ui->sampleCountSpin->value());
    const double noiseStdDev = ui->noiseSpin->value();

    dsp::SignalGenerator generator(sampleRate);

    QElapsedTimer timer;
    timer.start();

    auto signal = generator.sine({frequency, amplitude}, sampleCount);
    generator.addWhiteNoise(signal, noiseStdDev);

    const double elapsedMs = timer.nsecsElapsed() / 1e6;

    showSignal(ui->plotSignal, generator.timeAxis(sampleCount), signal);

    // Фильтрация появится на следующем шаге — пока нижний график пустой.
    ui->plotFiltered->graph(0)->data()->clear();
    ui->plotFiltered->replot();

    QString message = tr("Сгенерировано %1 отсчётов за %2 мс")
                          .arg(sampleCount)
                          .arg(elapsedMs, 0, 'f', 3);

    const double nyquist = sampleRate / 2.0;
    if (frequency > nyquist) {
        message += tr("  |  Внимание: частота выше частоты Найквиста (%1 Гц), будет наложение спектров")
                       .arg(nyquist);
    }
    statusBar()->showMessage(message);
}
