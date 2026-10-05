#pragma once

#include <QMainWindow>

#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCustomPlot;
class QCPGraph;
class QColor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBuildClicked();
    void onFilterTypeChanged(int index);

private:
    // Порядок совпадает с пунктами filterTypeCombo в MainWindow.ui.
    enum class FilterType {
        None = 0,
        MovingAverage = 1,
        LowPassFir = 2,
    };

    void setupPlot(QCustomPlot* plot, const QString& title);
    QCPGraph* makeGraph(QCustomPlot* plot, const QColor& color, const QString& name);
    void linkXAxis(QCustomPlot* source, QCustomPlot* target);

    std::unique_ptr<Ui::MainWindow> ui;

    QCPGraph* m_signalGraph = nullptr;    // верхний график: исходный сигнал
    QCPGraph* m_referenceGraph = nullptr; // нижний график: исходный сигнал бледным фоном
    QCPGraph* m_filteredGraph = nullptr;  // нижний график: результат фильтрации
};
