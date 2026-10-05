#pragma once

#include "dsp/Spectrum.h"

#include <vector>

namespace dsp {

// true, если проект собран с FFTW (CMake нашёл библиотеку).
bool fftwAvailable();

// БПФ вещественного сигнала средствами FFTW, возвращает N/2 + 1 бинов.
// Если проект собран без FFTW, бросает std::runtime_error.
std::vector<Complex> fftwReal(const std::vector<double>& x);

} // namespace dsp
