#include "dsp/Filters.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Индекс с продолжением края: всё левее нуля берём из x[0], правее конца — из x[N-1].
inline std::size_t clampIndex(std::ptrdiff_t i, std::size_t size)
{
    if (i < 0) {
        return 0;
    }
    if (static_cast<std::size_t>(i) >= size) {
        return size - 1;
    }
    return static_cast<std::size_t>(i);
}

void requireOdd(std::size_t length)
{
    if (length == 0 || length % 2 == 0) {
        throw std::invalid_argument("Window/kernel length must be odd");
    }
}

} // namespace

std::vector<double> convolveSame(const std::vector<double>& x, const std::vector<double>& h)
{
    requireOdd(h.size());

    const std::size_t n = x.size();
    std::vector<double> y(n, 0.0);
    const auto half = static_cast<std::ptrdiff_t>(h.size() / 2);

    for (std::size_t i = 0; i < n; ++i) {
        const auto center = static_cast<std::ptrdiff_t>(i);
        double acc = 0.0;
        for (std::size_t k = 0; k < h.size(); ++k) {
            acc += h[k] * x[clampIndex(center + half - static_cast<std::ptrdiff_t>(k), n)];
        }
        y[i] = acc;
    }
    return y;
}

std::vector<double> movingAverage(const std::vector<double>& x, std::size_t windowSize)
{
    requireOdd(windowSize);

    const std::size_t n = x.size();
    std::vector<double> y(n);
    if (n == 0) {
        return y;
    }

    const auto half = static_cast<std::ptrdiff_t>(windowSize / 2);
    const double scale = 1.0 / static_cast<double>(windowSize);

    // Сумма окна вокруг нулевого отсчёта.
    double sum = 0.0;
    for (std::ptrdiff_t j = -half; j <= half; ++j) {
        sum += x[clampIndex(j, n)];
    }
    y[0] = sum * scale;

    // Сдвигаем окно: добавляем отсчёт справа, убираем вышедший слева.
    for (std::size_t i = 1; i < n; ++i) {
        const auto c = static_cast<std::ptrdiff_t>(i);
        sum += x[clampIndex(c + half, n)] - x[clampIndex(c - half - 1, n)];
        y[i] = sum * scale;
    }
    return y;
}

std::vector<double> designLowPass(double cutoffHz, double sampleRateHz, std::size_t numTaps)
{
    requireOdd(numTaps);
    if (!(sampleRateHz > 0.0)) {
        throw std::invalid_argument("Sample rate must be positive");
    }
    if (!(cutoffHz > 0.0) || cutoffHz >= sampleRateHz / 2.0) {
        throw std::invalid_argument("Cutoff frequency must be in (0, fs/2)");
    }

    // Нормированная частота среза в долях частоты дискретизации (0 < fc < 0.5).
    const double fc = cutoffHz / sampleRateHz;
    const double middle = static_cast<double>(numTaps - 1) / 2.0;

    std::vector<double> h(numTaps);
    double sum = 0.0;
    for (std::size_t k = 0; k < numTaps; ++k) {
        const double t = static_cast<double>(k) - middle;

        // Идеальный ФНЧ: h(t) = sin(2*pi*fc*t) / (pi*t), в нуле предел равен 2*fc.
        const double ideal = (t == 0.0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);

        // Окно Хэмминга гасит боковые лепестки, возникающие из-за обрезки sinc.
        const double window = (numTaps == 1)
            ? 1.0
            : 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(k) / static_cast<double>(numTaps - 1));

        h[k] = ideal * window;
        sum += h[k];
    }

    // Нормировка: постоянная составляющая проходит без изменений.
    for (double& coeff : h) {
        coeff /= sum;
    }
    return h;
}

} // namespace dsp
