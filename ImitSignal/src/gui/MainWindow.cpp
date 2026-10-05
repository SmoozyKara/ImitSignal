#include "gui/MainWindow.h"
#include "ui_MainWindow.h"

#include "dsp/Fftw.h"
#include "dsp/Filters.h"
#include "dsp/Metrics.h"
#include "dsp/SignalGenerator.h"
#include "qcustomplot.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidgetItem>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace {

// Акцентные цвета подобраны так, чтобы читаться и на светлом, и на тёмном фоне.
const QColor kAccentBlue(45, 125, 230);
const QColor kAccentOrange(235, 110, 45);

const char* kStyleSheet = R"(
QFrame[role="card"] {
    background-color: palette(alternate-base);
    border: 1px solid palette(mid);
    border-radius: 6px;
}
QFrame[role="card"] QLabel {
    background: transparent;
    border: none;
}
QLabel[role="metricValue"] {
    font-size: 18px;
    font-weight: 600;
}
QLabel[role="warning"] {
    color: #d9534f;
}
)";

QVector<double> toQVector(const std::vector<double>& v)
{
    return QVector<double>(v.begin(), v.end());
}

// Короткие операции меряем несколько раз и берём минимум: так меньше влияния
// планировщика ОС и холодного кэша. Длинные (>30 мс суммарно) — один раз.
template <typename Fn>
double measureMs(Fn&& fn)
{
    double best = std::numeric_limits<double>::max();
    double total = 0.0;
    int runs = 0;
    QElapsedTimer timer;
    do {
        timer.start();
        fn();
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        best = std::min(best, ms);
        total += ms;
        ++runs;
    } while (runs < 50 && total < 30.0);
    return best;
}

QString formatMs(double ms)
{
    const int decimals = ms < 1.0 ? 3 : (ms < 100.0 ? 2 : 0);
    return QObject::tr("%1 мс").arg(ms, 0, 'f', decimals);
}

QString formatHz(double hz)
{
    const int decimals = hz < 10.0 ? 3 : (hz < 1000.0 ? 2 : 1);
    return QObject::tr("%1 Гц").arg(hz, 0, 'f', decimals);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
    , m_noiseSeed(std::random_device{}())
{
    ui->setupUi(this);
    setStyleSheet(QString::fromUtf8(kStyleSheet));

    // Подписи под крупными цифрами — приглушённым цветом системы.
    for (QLabel* label : findChildren<QLabel*>()) {
        if (label->property("role").toString() == QLatin1String("metricCaption")) {
            label->setForegroundRole(QPalette::PlaceholderText);
        }
    }
    setWarning(ui->signalWarningLabel, {});
    setWarning(ui->spectrumWarningLabel, {});

    // --- Вкладка «Сигнал и фильтр» -------------------------------------------
    setupPlot(ui->plotSignal, tr("Исходный сигнал"), tr("Время, с"), tr("Амплитуда"));
    setupPlot(ui->plotFiltered, tr("После фильтрации"), tr("Время, с"), tr("Амплитуда"));

    m_signalGraph = ui->plotSignal->addGraph();
    m_signalGraph->setName(tr("Сигнал"));
    // Исходный сигнал добавляется первым, поэтому рисуется под результатом.
    m_referenceGraph = ui->plotFiltered->addGraph();
    m_referenceGraph->setName(tr("Исходный"));
    m_filteredGraph = ui->plotFiltered->addGraph();
    m_filteredGraph->setName(tr("После фильтра"));
    ui->plotFiltered->legend->setVisible(true);

    // Масштаб и сдвиг по времени у двух графиков синхронны.
    linkXAxis(ui->plotSignal, ui->plotFiltered);
    linkXAxis(ui->plotFiltered, ui->plotSignal);

    ui->filterTypeCombo->setCurrentIndex(static_cast<int>(FilterType::LowPassFir));
    onFilterTypeChanged(ui->filterTypeCombo->currentIndex());

    // --- Вкладка «Спектр» ----------------------------------------------------
    m_spectrumTitle = setupPlot(ui->plotSpectrum, tr("Амплитудный спектр"), tr("Частота, Гц"), tr("Амплитуда"));
    m_spectrumGraph = ui->plotSpectrum->addGraph();

    m_peakTracer = new QCPItemTracer(ui->plotSpectrum);
    m_peakTracer->setGraph(m_spectrumGraph);
    m_peakTracer->setInterpolating(false);
    m_peakTracer->setStyle(QCPItemTracer::tsCircle);
    m_peakTracer->setSize(9);

    m_peakLabel = new QCPItemText(ui->plotSpectrum);
    m_peakLabel->position->setParentAnchor(m_peakTracer->position); // координаты — смещение в пикселях
    m_peakLabel->position->setCoords(0, -10);
    m_peakLabel->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    setupMethodsTable();
    if (!dsp::fftwAvailable()) {
        ui->fftwStatusLabel->setText(
            tr("FFTW не найдена. Чтобы сравнить с ней, выполните «brew install fftw» и перезапустите CMake."));
    } else {
        ui->fftwStatusLabel->hide();
    }

    applyTheme();
    m_themeReady = true;

    // --- Автообновление --------------------------------------------------------
    // Пересчёт запускается с небольшой задержкой: при прокрутке значения
    // колесом мыши считаем один раз, а не на каждый шаг.
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(150);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &MainWindow::rebuild);

    for (QDoubleSpinBox* spin : {ui->frequencySpin, ui->amplitudeSpin, ui->sampleRateSpin, ui->noiseSpin,
                                 ui->interferenceFrequencySpin, ui->interferenceAmplitudeSpin, ui->cutoffSpin}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &MainWindow::scheduleRebuild);
    }
    for (QSpinBox* spin : {ui->sampleCountSpin, ui->windowSizeSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, &MainWindow::scheduleRebuild);
    }
    connect(ui->filterTypeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onFilterTypeChanged);
    connect(ui->filterTypeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::scheduleRebuild);
    connect(ui->newNoiseButton, &QPushButton::clicked, this, &MainWindow::onNewNoiseClicked);

    connect(ui->spectrumSourceCombo, &QComboBox::currentIndexChanged, this, &MainWindow::recomputeSpectrum);
    connect(ui->spectrumWindowCombo, &QComboBox::currentIndexChanged, this, &MainWindow::recomputeSpectrum);
    connect(ui->spectrumDbCheck, &QCheckBox::toggled, this, &MainWindow::plotSpectrum);
    connect(ui->methodsTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onMethodSelectionChanged);

    rebuild();
}

MainWindow::~MainWindow() = default;

// ============================================================================
// Оформление
// ============================================================================

QCPTextElement* MainWindow::setupPlot(QCustomPlot* plot, const QString& title, const QString& xLabel,
                                      const QString& yLabel)
{
    plot->xAxis->setLabel(xLabel);
    plot->yAxis->setLabel(yLabel);

    QFont titleFont = font();
    titleFont.setBold(true);
    auto* titleElement = new QCPTextElement(plot, title, titleFont);
    plot->plotLayout()->insertRow(0);
    plot->plotLayout()->addElement(0, 0, titleElement);
    m_plotTitles.push_back(titleElement);

    // Перетаскивание и масштабирование колесом мыши.
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    return titleElement;
}

void MainWindow::applyTheme()
{
    // Цвета берём из системной палитры, чтобы графики не оставались белыми в тёмной теме.
    const QPalette pal = palette();
    const QColor background = pal.color(QPalette::Base);
    const QColor foreground = pal.color(QPalette::Text);
    QColor gridColor = foreground;
    gridColor.setAlpha(45);
    QColor faded = foreground;
    faded.setAlpha(70);

    for (QCustomPlot* plot : {ui->plotSignal, ui->plotFiltered, ui->plotSpectrum}) {
        plot->setBackground(background);
        for (QCPAxis* axis : {plot->xAxis, plot->yAxis}) {
            axis->setBasePen(QPen(foreground));
            axis->setTickPen(QPen(foreground));
            axis->setSubTickPen(QPen(foreground));
            axis->setTickLabelColor(foreground);
            axis->setLabelColor(foreground);
            axis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
            axis->grid()->setZeroLinePen(QPen(gridColor));
        }
    }
    for (QCPTextElement* title : m_plotTitles) {
        title->setTextColor(foreground);
    }

    QColor legendBackground = background;
    legendBackground.setAlpha(220);
    ui->plotFiltered->legend->setBrush(legendBackground);
    ui->plotFiltered->legend->setTextColor(foreground);
    ui->plotFiltered->legend->setBorderPen(QPen(gridColor));

    m_signalGraph->setPen(QPen(kAccentBlue));
    m_referenceGraph->setPen(QPen(faded));
    m_filteredGraph->setPen(QPen(kAccentOrange, 1.5));
    m_spectrumGraph->setPen(QPen(kAccentBlue));
    m_peakTracer->setPen(QPen(kAccentOrange, 2));
    m_peakTracer->setBrush(Qt::NoBrush);
    m_peakLabel->setColor(foreground);

    for (QCustomPlot* plot : {ui->plotSignal, ui->plotFiltered, ui->plotSpectrum}) {
        plot->replot(QCustomPlot::rpQueuedReplot);
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (m_themeReady && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)) {
        applyTheme();
    }
}

void MainWindow::setWarning(QLabel* label, const QString& text)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
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

// ============================================================================
// Вкладка «Сигнал и фильтр»
// ============================================================================

void MainWindow::scheduleRebuild()
{
    m_rebuildTimer.start();
}

void MainWindow::onNewNoiseClicked()
{
    m_noiseSeed = std::random_device{}();
    rebuild();
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

void MainWindow::rebuild()
{
    m_rebuildTimer.stop();

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
        const QSignalBlocker blocker(ui->windowSizeSpin);
        ui->windowSizeSpin->setValue(windowSize);
    }

    QStringList warnings;

    // --- Генерация ------------------------------------------------------------
    // Зерно шума фиксировано, пока не нажата «Новая реализация шума»:
    // при изменении параметров меняется сигнал, а не случайная картинка шума.
    std::vector<double> signal;
    const double generationMs = measureMs([&] {
        dsp::SignalGenerator generator(sampleRate);
        generator.setSeed(m_noiseSeed);
        signal = generator.sine(mainSine, sampleCount);
        if (interference.amplitude > 0.0) {
            generator.addSine(signal, interference);
        }
        generator.addWhiteNoise(signal, noiseStdDev);
    });

    dsp::SignalGenerator timeline(sampleRate);
    const QVector<double> time = toQVector(timeline.timeAxis(sampleCount));
    const QVector<double> signalData = toQVector(signal);
    m_signalGraph->setData(time, signalData, true);
    m_referenceGraph->setData(time, signalData, true);

    // --- Фильтрация -----------------------------------------------------------
    std::vector<double> filtered;
    double filterMs = 0.0;
    if (filterType == FilterType::LowPassFir && cutoff >= nyquist) {
        warnings << tr("Частота среза должна быть меньше %1 (половины частоты дискретизации).").arg(formatHz(nyquist));
    } else {
        try {
            const auto window = static_cast<std::size_t>(windowSize);
            switch (filterType) {
            case FilterType::None:
                filtered = signal;
                break;
            case FilterType::MovingAverage:
                filterMs = measureMs([&] { filtered = dsp::movingAverage(signal, window); });
                break;
            case FilterType::LowPassFir:
                filterMs = measureMs([&] {
                    const auto taps = dsp::designLowPass(cutoff, sampleRate, window);
                    filtered = dsp::convolveSame(signal, taps);
                });
                break;
            }
        } catch (const std::exception& e) {
            filtered.clear();
            warnings << tr("Ошибка фильтра: %1").arg(QString::fromUtf8(e.what()));
        }
    }

    if (filtered.empty()) {
        m_filteredGraph->data()->clear();
    } else {
        m_filteredGraph->setData(time, toQVector(filtered), true);
    }

    // --- Метрики --------------------------------------------------------------
    // Ошибка — отклонение от чистого полезного синуса (без помехи и шума).
    // Края длиной в полокна не учитываем: там работает продление крайними отсчётами.
    const std::vector<double> clean = timeline.sine(mainSine, sampleCount);
    const std::size_t margin = filterType == FilterType::None ? 0 : static_cast<std::size_t>(windowSize / 2);
    const double errorBefore = dsp::rmsDifference(signal, clean, margin);

    ui->genTimeValue->setText(formatMs(generationMs));
    ui->errorBeforeValue->setText(QString::number(errorBefore, 'f', 4));

    if (filtered.empty() || filterType == FilterType::None) {
        ui->filterTimeValue->setText(QStringLiteral("—"));
        ui->errorAfterValue->setText(QStringLiteral("—"));
        ui->gainValue->setText(QStringLiteral("—"));
    } else {
        const double errorAfter = dsp::rmsDifference(filtered, clean, margin);
        ui->filterTimeValue->setText(formatMs(filterMs));
        ui->errorAfterValue->setText(QString::number(errorAfter, 'f', 4));
        if (errorAfter > 0.0 && errorBefore > 0.0) {
            const double gainDb = 20.0 * std::log10(errorBefore / errorAfter);
            ui->gainValue->setText(tr("%1%2 дБ").arg(gainDb >= 0 ? QStringLiteral("+") : QString()).arg(gainDb, 0, 'f', 1));
        } else {
            ui->gainValue->setText(QStringLiteral("—"));
        }
    }

    if (mainSine.frequencyHz > nyquist || (interference.amplitude > 0.0 && interference.frequencyHz > nyquist)) {
        warnings << tr("Частота выше частоты Найквиста (%1): на графике будет наложение спектров.")
                        .arg(formatHz(nyquist));
    }
    setWarning(ui->signalWarningLabel, warnings.join(QLatin1Char('\n')));

    ui->plotSignal->rescaleAxes();
    ui->plotFiltered->rescaleAxes();
    ui->plotSignal->replot();
    ui->plotFiltered->replot();

    // Запоминаем данные для вкладки «Спектр» и сразу пересчитываем её.
    m_signal = std::move(signal);
    m_filtered = std::move(filtered);
    m_sampleRate = sampleRate;
    recomputeSpectrum();
}

// ============================================================================
// Вкладка «Спектр»
// ============================================================================

QString MainWindow::methodName(SpectrumMethod method) const
{
    switch (method) {
    case SpectrumMethod::NaiveDft:
        return tr("ДПФ по определению");
    case SpectrumMethod::OwnFft:
        return tr("БПФ (своё, radix-2)");
    case SpectrumMethod::Fftw:
        return tr("FFTW");
    }
    return {};
}

void MainWindow::setupMethodsTable()
{
    QTableWidget* table = ui->methodsTable;
    table->setColumnCount(3);
    table->setRowCount(kMethodCount);
    table->setHorizontalHeaderLabels({tr("Метод"), tr("Время"), tr("Расхождение")});
    table->horizontalHeaderItem(1)->setToolTip(tr("Минимум из нескольких запусков. У FFTW включает создание плана."));
    table->horizontalHeaderItem(2)->setToolTip(tr("max|X − X_эталон| / max|X_эталон|"));

    for (int row = 0; row < kMethodCount; ++row) {
        table->setItem(row, 0, new QTableWidgetItem(methodName(static_cast<SpectrumMethod>(row))));
        for (int column = 1; column < 3; ++column) {
            auto* item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, column, item);
        }
    }

    QHeaderView* header = table->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    // Таблица ровно по высоте трёх строк, без пустого места и прокрутки.
    // До показа окна у заголовка ещё нет геометрии, поэтому берём sizeHint.
    table->setFixedHeight(header->sizeHint().height() + kMethodCount * table->verticalHeader()->defaultSectionSize()
                          + 2 * table->frameWidth() + 2);
}

void MainWindow::clearSpectrum(const QString& reason)
{
    for (MethodResult& result : m_methodResults) {
        result = MethodResult{};
    }
    fillMethodsTable();

    m_spectrumGraph->data()->clear();
    m_peakTracer->setVisible(false);
    m_peakLabel->setVisible(false);
    m_spectrumTitle->setText(tr("Амплитудный спектр"));
    ui->plotSpectrum->replot();

    for (QLabel* label : {ui->frameSizeValue, ui->resolutionValue, ui->peakFrequencyValue, ui->peakAmplitudeValue}) {
        label->setText(QStringLiteral("—"));
    }
    setWarning(ui->spectrumWarningLabel, reason);
}

void MainWindow::recomputeSpectrum()
{
    const auto source = static_cast<SpectrumSource>(ui->spectrumSourceCombo->currentIndex());
    const auto windowType = ui->spectrumWindowCombo->currentIndex() == 1 ? dsp::WindowType::Hann
                                                                         : dsp::WindowType::Rectangular;

    if (source == SpectrumSource::Filtered && m_filtered.empty() && !m_signal.empty()) {
        clearSpectrum(tr("Фильтр не применён: проверьте его параметры на вкладке «Сигнал и фильтр»."));
        return;
    }
    const std::vector<double>& data = (source == SpectrumSource::Filtered) ? m_filtered : m_signal;
    if (data.size() < 2 || m_sampleRate <= 0.0) {
        clearSpectrum(tr("Нет данных: задайте сигнал на вкладке «Сигнал и фильтр»."));
        return;
    }

    // Все методы считают один и тот же кадр длиной в степень двойки,
    // иначе сравнивать их время и результат было бы нечестно.
    const std::size_t n = dsp::floorPowerOfTwo(data.size());
    const std::vector<double> window = dsp::makeWindow(windowType, n);
    std::vector<double> frame(n);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        frame[i] = data[i] * window[i];
        windowSum += window[i];
    }

    m_frameSize = n;
    m_sourceSize = data.size();
    m_windowSum = windowSum;

    for (int i = 0; i < kMethodCount; ++i) {
        const auto method = static_cast<SpectrumMethod>(i);
        MethodResult& result = m_methodResults[static_cast<std::size_t>(i)];
        result = MethodResult{};

        if (method == SpectrumMethod::NaiveDft && n > kMaxDftSize) {
            result.unavailableReason = tr("N > %1, O(N²)").arg(kMaxDftSize);
            continue;
        }
        if (method == SpectrumMethod::Fftw && !dsp::fftwAvailable()) {
            result.unavailableReason = tr("не установлена");
            continue;
        }

        try {
            result.timeMs = measureMs([&] {
                switch (method) {
                case SpectrumMethod::NaiveDft:
                    result.bins = dsp::dftReal(frame);
                    break;
                case SpectrumMethod::OwnFft:
                    result.bins = dsp::fftReal(frame);
                    break;
                case SpectrumMethod::Fftw:
                    result.bins = dsp::fftwReal(frame);
                    break;
                }
            });
            result.computed = true;
        } catch (const std::exception& e) {
            result.bins.clear();
            result.unavailableReason = QString::fromUtf8(e.what());
        }
    }

    // Если выбранный метод недоступен на этом кадре — показываем своё БПФ.
    if (!m_methodResults[static_cast<std::size_t>(m_selectedMethod)].computed) {
        m_selectedMethod = SpectrumMethod::OwnFft;
    }

    setWarning(ui->spectrumWarningLabel, {});
    fillMethodsTable();
    plotSpectrum();
}

void MainWindow::fillMethodsTable()
{
    // Эталон для расхождения: FFTW, если она есть, иначе ДПФ по определению, иначе своё БПФ.
    int reference = -1;
    for (SpectrumMethod candidate : {SpectrumMethod::Fftw, SpectrumMethod::NaiveDft, SpectrumMethod::OwnFft}) {
        if (m_methodResults[static_cast<std::size_t>(candidate)].computed) {
            reference = static_cast<int>(candidate);
            break;
        }
    }

    const QSignalBlocker blocker(ui->methodsTable);
    for (int row = 0; row < kMethodCount; ++row) {
        const MethodResult& result = m_methodResults[static_cast<std::size_t>(row)];
        QTableWidgetItem* nameItem = ui->methodsTable->item(row, 0);
        QTableWidgetItem* timeItem = ui->methodsTable->item(row, 1);
        QTableWidgetItem* diffItem = ui->methodsTable->item(row, 2);

        const Qt::ItemFlags flags = result.computed ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
        for (QTableWidgetItem* item : {nameItem, timeItem, diffItem}) {
            item->setFlags(flags);
        }

        if (!result.computed) {
            timeItem->setText(QStringLiteral("—"));
            diffItem->setText(result.unavailableReason);
            continue;
        }

        timeItem->setText(formatMs(result.timeMs));
        if (row == reference) {
            diffItem->setText(tr("эталон"));
        } else {
            const double diff = dsp::maxRelativeDifference(
                result.bins, m_methodResults[static_cast<std::size_t>(reference)].bins);
            diffItem->setText(QString::number(diff, 'e', 1));
        }
    }

    const int selectedRow = static_cast<int>(m_selectedMethod);
    if (m_methodResults[static_cast<std::size_t>(selectedRow)].computed) {
        ui->methodsTable->selectRow(selectedRow);
    } else {
        ui->methodsTable->clearSelection();
    }
}

void MainWindow::onMethodSelectionChanged()
{
    const QList<QTableWidgetItem*> selected = ui->methodsTable->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const int row = selected.first()->row();
    if (!m_methodResults[static_cast<std::size_t>(row)].computed) {
        return;
    }
    m_selectedMethod = static_cast<SpectrumMethod>(row);
    plotSpectrum();
}

void MainWindow::plotSpectrum()
{
    const MethodResult& result = m_methodResults[static_cast<std::size_t>(m_selectedMethod)];
    if (!result.computed || m_frameSize == 0) {
        return;
    }

    const bool useDb = ui->spectrumDbCheck->isChecked();
    const double binWidth = m_sampleRate / static_cast<double>(m_frameSize);
    const std::vector<double> amplitude = dsp::amplitudeSpectrum(result.bins, m_frameSize, m_windowSum);

    QVector<double> freq(static_cast<int>(amplitude.size()));
    QVector<double> values(static_cast<int>(amplitude.size()));
    for (std::size_t k = 0; k < amplitude.size(); ++k) {
        const int i = static_cast<int>(k);
        freq[i] = static_cast<double>(k) * binWidth;
        // Ноль в логарифм не подставить: ограничиваем снизу 1e-12 (−240 дБ).
        values[i] = useDb ? 20.0 * std::log10(std::max(amplitude[k], 1e-12)) : amplitude[k];
    }
    m_spectrumGraph->setData(freq, values, true);

    // Самый сильный пик без учёта постоянной составляющей.
    std::size_t peak = 0;
    if (amplitude.size() > 1) {
        peak = static_cast<std::size_t>(std::max_element(amplitude.begin() + 1, amplitude.end()) - amplitude.begin());
    }
    const double peakHz = static_cast<double>(peak) * binWidth;
    const double peakValue = values[static_cast<int>(peak)];

    m_peakTracer->setGraphKey(peakHz);
    m_peakTracer->setVisible(true);
    m_peakLabel->setText(formatHz(peakHz));
    m_peakLabel->setVisible(true);

    // --- Оси ------------------------------------------------------------------
    ui->plotSpectrum->yAxis->setLabel(useDb ? tr("Амплитуда, дБ") : tr("Амплитуда"));
    ui->plotSpectrum->rescaleAxes();
    if (useDb) {
        // Численный «пол» около −300 дБ сжал бы всё полезное в полоску: показываем 160 дБ от пика.
        const double lower = std::max(ui->plotSpectrum->yAxis->range().lower, peakValue - 160.0);
        ui->plotSpectrum->yAxis->setRange(lower, peakValue + 15.0);
    } else {
        // Запас сверху, чтобы подпись пика не обрезалась.
        ui->plotSpectrum->yAxis->setRange(0.0, std::max(peakValue, 1e-12) * 1.2);
    }
    m_spectrumTitle->setText(tr("Амплитудный спектр — %1").arg(methodName(m_selectedMethod)));
    ui->plotSpectrum->replot();

    // --- Метрики --------------------------------------------------------------
    ui->frameSizeValue->setText(m_frameSize == m_sourceSize ? QString::number(m_frameSize)
                                                            : tr("%1 из %2").arg(m_frameSize).arg(m_sourceSize));
    ui->resolutionValue->setText(formatHz(binWidth));
    ui->peakFrequencyValue->setText(formatHz(peakHz));
    ui->peakAmplitudeValue->setText(QString::number(amplitude[peak], 'f', 3));
}
