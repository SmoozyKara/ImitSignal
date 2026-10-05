#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace dsp {

using Complex = std::complex<double>;

enum class WindowType {
    Rectangular,
    Hann,
};

bool isPowerOfTwo(std::size_t n);

// Наибольшая степень двойки, не превосходящая n (n >= 1).
std::size_t floorPowerOfTwo(std::size_t n);

// Весовое окно длины size для спектрального анализа (периодический вариант).
std::vector<double> makeWindow(WindowType type, std::size_t size);

// ДПФ вещественного сигнала по определению: X[k] = sum_n x[n] * e^(-j*2*pi*k*n/N).
// Возвращает N/2 + 1 бинов (остальные для вещественного сигнала симметричны). O(N^2).
std::vector<Complex> dftReal(const std::vector<double>& x);

// БПФ Кули–Тьюки с основанием 2, N должно быть степенью двойки.
// Возвращает N/2 + 1 бинов. O(N log N).
std::vector<Complex> fftReal(const std::vector<double>& x);

// Амплитудный спектр: синус амплитуды A, попавший точно в бин, даёт значение A.
// signalLength — длина исходного кадра N, windowSum — сумма коэффициентов окна.
std::vector<double> amplitudeSpectrum(const std::vector<Complex>& bins,
                                      std::size_t signalLength,
                                      double windowSum);

// max|a[k] - b[k]| / max|b[k]| — для сверки разных реализаций преобразования.
double maxRelativeDifference(const std::vector<Complex>& a, const std::vector<Complex>& b);

} // namespace dsp
