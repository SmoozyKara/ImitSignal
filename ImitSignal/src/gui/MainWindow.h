#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCustomPlot;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBuildClicked();

private:
    void setupPlot(QCustomPlot* plot, const QString& title);
    void showSignal(QCustomPlot* plot,
                    const std::vector<double>& time,
                    const std::vector<double>& values);

    std::unique_ptr<Ui::MainWindow> ui;
};
