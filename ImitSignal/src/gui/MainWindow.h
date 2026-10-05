#pragma once

#include "dsp/Spectrum.h"

#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <array>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCustomPlot;
class QCPGraph;
class QCPItemText;
class QCPItemTracer;
class QCPTextElement;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void scheduleRebuild();
    void rebuild();
    void onNewNoiseClicked();
    void onFilterTypeChanged(int index);
    void recomputeSpectrum();
    void onMethodSelectionChanged();
    void plotSpectrum();

private:
    // Порядок значений совпадает с порядком пунктов в MainWindow.ui.
    enum class FilterType {
        None = 0,
        MovingAverage = 1,
        LowPassFir = 2,
    };
    enum class SpectrumSource {
        Original = 0,
        Filtered = 1,
    };
    // Порядок совпадает со строками таблицы методов.
    enum class SpectrumMethod {
        NaiveDft = 0,
        OwnFft = 1,
        Fftw = 2,
    };
    static constexpr int kMethodCount = 3;

    // Наивное ДПФ растёт как O(N^2): дальше этого размера интерфейс заметно подвисает.
    static constexpr std::size_t kMaxDftSize = 16384;

    struct MethodResult {
        bool computed = false;
        double timeMs = 0.0;
        std::vector<dsp::Complex> bins;
        QString unavailableReason;
    };

    QCPTextElement* setupPlot(QCustomPlot* plot, const QString& title, const QString& xLabel, const QString& yLabel);
    void linkXAxis(QCustomPlot* source, QCustomPlot* target);
    void setupMethodsTable();
    void fillMethodsTable();
    void applyTheme();
    void clearSpectrum(const QString& reason);
    void setWarning(QLabel* label, const QString& text);
    QString methodName(SpectrumMethod method) const;

    std::unique_ptr<Ui::MainWindow> ui;
    QTimer m_rebuildTimer;
    unsigned m_noiseSeed = 0;
    bool m_themeReady = false;

    QCPGraph* m_signalGraph = nullptr;    // вкладка 1, верх: исходный сигнал
    QCPGraph* m_referenceGraph = nullptr; // вкладка 1, низ: исходный сигнал бледным фоном
    QCPGraph* m_filteredGraph = nullptr;  // вкладка 1, низ: результат фильтрации
    QCPGraph* m_spectrumGraph = nullptr;  // вкладка 2: амплитудный спектр
    QCPTextElement* m_spectrumTitle = nullptr;
    QCPItemTracer* m_peakTracer = nullptr;
    QCPItemText* m_peakLabel = nullptr;
    std::vector<QCPTextElement*> m_plotTitles;

    // Последние построенные данные: из них считается спектр.
    std::vector<double> m_signal;
    std::vector<double> m_filtered;
    double m_sampleRate = 0.0;

    // Результаты всех методов для текущего кадра спектра.
    std::array<MethodResult, kMethodCount> m_methodResults;
    SpectrumMethod m_selectedMethod = SpectrumMethod::OwnFft;
    std::size_t m_frameSize = 0;
    std::size_t m_sourceSize = 0;
    double m_windowSum = 0.0;
};
