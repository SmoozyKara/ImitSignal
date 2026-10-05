#pragma once

#include <cstddef>
#include <vector>

namespace dsp {

// Свёртка сигнала с центрированным ядром нечётной длины M:
//   y[n] = sum_k h[k] * x[n + (M-1)/2 - k]
// Выход той же длины, что и вход. Ядро центрировано, поэтому у симметричного
// фильтра нет задержки на (M-1)/2 отсчётов. За краями сигнал продолжается
// крайними отсчётами (edge replication).
// Сложность O(N * M).
std::vector<double> convolveSame(const std::vector<double>& x, const std::vector<double>& h);

// Скользящее среднее с окном нечётной длины через бегущую сумму.
// Результат тот же, что у convolveSame с ядром 1/M, но сложность O(N)
// и не зависит от ширины окна.
std::vector<double> movingAverage(const std::vector<double>& x, std::size_t windowSize);

// Коэффициенты КИХ-фильтра нижних частот методом взвешенного sinc
// (окно Хэмминга). Длина нечётная, коэффициенты нормированы так,
// что усиление на нулевой частоте равно 1.
std::vector<double> designLowPass(double cutoffHz, double sampleRateHz, std::size_t numTaps);

} // namespace dsp
