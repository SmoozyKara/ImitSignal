#include "dsp/SignalGenerator.h"

#include <cmath>
#include <stdexcept>

namespace dsp {

namespace {
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
}

SignalGenerator::SignalGenerator(double sampleRateHz)
    : m_sampleRate(sampleRateHz)
    , m_rng(std::random_device{}())
{
    if (!(sampleRateHz > 0.0)) {
        throw std::invalid_argument("Sample rate must be positive");
    }
}

std::vector<double> SignalGenerator::sine(const SineParams& params, std::size_t sampleCount) const
{
    std::vector<double> out(sampleCount, 0.0);
    addSine(out, params);
    return out;
}

void SignalGenerator::addSine(std::vector<double>& signal, const SineParams& params) const
{
    // Нормированная угловая частота: на сколько радиан фаза растёт за один отсчёт.
    const double omega = kTwoPi * params.frequencyHz / m_sampleRate;

    for (std::size_t i = 0; i < signal.size(); ++i) {
        signal[i] += params.amplitude * std::sin(omega * static_cast<double>(i) + params.phaseRad);
    }
}

void SignalGenerator::addWhiteNoise(std::vector<double>& signal, double stdDev)
{
    if (stdDev <= 0.0) {
        return;
    }
    std::normal_distribution<double> dist(0.0, stdDev);
    for (double& x : signal) {
        x += dist(m_rng);
    }
}

std::vector<double> SignalGenerator::timeAxis(std::size_t sampleCount) const
{
    std::vector<double> t(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        t[i] = static_cast<double>(i) / m_sampleRate;
    }
    return t;
}

} // namespace dsp
