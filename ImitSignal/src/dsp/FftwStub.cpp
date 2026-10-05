// Заглушка: используется, когда CMake не нашёл FFTW.
#include "dsp/Fftw.h"

#include <stdexcept>

namespace dsp {

bool fftwAvailable()
{
    return false;
}

std::vector<Complex> fftwReal(const std::vector<double>&)
{
    throw std::runtime_error("SignalLab is built without FFTW");
}

} // namespace dsp
