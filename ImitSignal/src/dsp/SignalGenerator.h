#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace dsp {

struct SineParams {
    double frequencyHz = 1.0;
    double amplitude = 1.0;
    double phaseRad = 0.0;
};

// Генератор дискретных сигналов с заданной частотой дискретизации.
class SignalGenerator {
public:
    explicit SignalGenerator(double sampleRateHz);

    double sampleRate() const { return m_sampleRate; }

    // x[i] = A * sin(2*pi*f*i/fs + phi), i = 0..sampleCount-1
    std::vector<double> sine(const SineParams& params, std::size_t sampleCount) const;

    // Прибавляет к уже существующему сигналу ещё один синус (например, помеху).
    void addSine(std::vector<double>& signal, const SineParams& params) const;

    // Добавляет к сигналу гауссов белый шум с заданным СКО.
    void addWhiteNoise(std::vector<double>& signal, double stdDev);

    // Отсчёты времени t[i] = i / fs, в секундах.
    std::vector<double> timeAxis(std::size_t sampleCount) const;

    // Фиксированное зерно — для воспроизводимых результатов (тесты, сравнения).
    void setSeed(unsigned seed) { m_rng.seed(seed); }

private:
    double m_sampleRate;
    std::mt19937 m_rng;
};

} // namespace dsp
