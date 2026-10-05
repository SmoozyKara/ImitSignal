#include "dsp/Spectrum.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dsp {

namespace {

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

// Таблица поворотных множителей W[m] = e^(-j*2*pi*m/N), m = 0..count-1.
std::vector<Complex> twiddles(std::size_t n, std::size_t count)
{
    std::vector<Complex> w(count);
    for (std::size_t m = 0; m < count; ++m) {
        w[m] = std::polar(1.0, -kTwoPi * static_cast<double>(m) / static_cast<double>(n));
    }
    return w;
}

// Итеративное БПФ на месте: перестановка с обращением битов + log2(N) этапов «бабочек».
void fftInPlace(std::vector<Complex>& a)
{
    const std::size_t n = a.size();

    // 1. Перестановка: элемент с индексом i меняется местами с элементом,
    //    у которого биты индекса записаны в обратном порядке.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    // 2. Этапы: на каждом объединяем пары ДПФ длины len/2 в ДПФ длины len.
    //    Поворотные множители берём из общей таблицы с шагом n/len —
    //    так точнее, чем накапливать их умножением.
    const std::vector<Complex> w = twiddles(n, n / 2);
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t step = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const Complex u = a[start + k];
                const Complex v = a[start + k + half] * w[k * step];
                a[start + k] = u + v;
                a[start + k + half] = u - v;
            }
        }
    }
}

} // namespace

bool isPowerOfTwo(std::size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

std::size_t floorPowerOfTwo(std::size_t n)
{
    if (n == 0) {
        throw std::invalid_argument("n must be positive");
    }
    std::size_t p = 1;
    while (p <= n / 2) {
        p <<= 1;
    }
    return p;
}

std::vector<double> makeWindow(WindowType type, std::size_t size)
{
    std::vector<double> w(size, 1.0);
    if (type == WindowType::Hann) {
        for (std::size_t i = 0; i < size; ++i) {
            w[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(size));
        }
    }
    return w;
}

std::vector<Complex> dftReal(const std::vector<double>& x)
{
    const std::size_t n = x.size();
    if (n == 0) {
        return {};
    }

    // sin/cos считаем один раз; индекс (k*t) mod N ведём сложением, без умножения и деления.
    const std::vector<Complex> w = twiddles(n, n);
    std::vector<Complex> bins(n / 2 + 1);

    for (std::size_t k = 0; k < bins.size(); ++k) {
        Complex acc = 0.0;
        std::size_t index = 0;
        for (std::size_t t = 0; t < n; ++t) {
            acc += x[t] * w[index];
            index += k;
            if (index >= n) {
                index -= n;
            }
        }
        bins[k] = acc;
    }
    return bins;
}

std::vector<Complex> fftReal(const std::vector<double>& x)
{
    const std::size_t n = x.size();
    if (!isPowerOfTwo(n)) {
        throw std::invalid_argument("FFT size must be a power of two");
    }

    std::vector<Complex> a(x.begin(), x.end());
    fftInPlace(a);
    a.resize(n / 2 + 1);
    return a;
}

std::vector<double> amplitudeSpectrum(const std::vector<Complex>& bins,
                                      std::size_t signalLength,
                                      double windowSum)
{
    std::vector<double> amp(bins.size());
    if (bins.empty() || windowSum <= 0.0) {
        return amp;
    }

    // Энергия синуса делится между бинами k и N-k, поэтому множитель 2.
    // Исключения — постоянная составляющая (k = 0) и частота Найквиста (k = N/2 при чётном N).
    const bool hasNyquistBin = signalLength % 2 == 0;
    for (std::size_t k = 0; k < bins.size(); ++k) {
        const bool single = (k == 0) || (hasNyquistBin && k == bins.size() - 1);
        amp[k] = std::abs(bins[k]) * (single ? 1.0 : 2.0) / windowSum;
    }
    return amp;
}

double maxRelativeDifference(const std::vector<Complex>& a, const std::vector<Complex>& b)
{
    if (a.size() != b.size()) {
        throw std::invalid_argument("Spectra have different sizes");
    }
    double maxDiff = 0.0;
    double maxRef = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        maxDiff = std::max(maxDiff, std::abs(a[k] - b[k]));
        maxRef = std::max(maxRef, std::abs(b[k]));
    }
    return maxRef > 0.0 ? maxDiff / maxRef : maxDiff;
}

} // namespace dsp
