// Реализация через FFTW. Компилируется, только если CMake нашёл библиотеку.
#include "dsp/Fftw.h"

#include <fftw3.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace dsp {

namespace {

struct FftwFree {
    void operator()(void* p) const { fftw_free(p); }
};

using FftwBuffer = std::unique_ptr<void, FftwFree>;
using FftwPlan = std::unique_ptr<std::remove_pointer_t<fftw_plan>, decltype(&fftw_destroy_plan)>;

} // namespace

bool fftwAvailable()
{
    return true;
}

std::vector<Complex> fftwReal(const std::vector<double>& x)
{
    const std::size_t n = x.size();
    if (n == 0) {
        return {};
    }
    if (n > static_cast<std::size_t>(INT_MAX)) {
        throw std::invalid_argument("Signal is too long for FFTW plan");
    }
    const std::size_t outSize = n / 2 + 1;

    // fftw_malloc выравнивает память под SIMD-инструкции.
    FftwBuffer inBuffer(fftw_malloc(sizeof(double) * n));
    FftwBuffer outBuffer(fftw_malloc(sizeof(fftw_complex) * outSize));
    if (!inBuffer || !outBuffer) {
        throw std::bad_alloc();
    }
    auto* in = static_cast<double*>(inBuffer.get());
    auto* out = static_cast<fftw_complex*>(outBuffer.get());

    // План создаётся до заполнения входа: с флагами вроде FFTW_MEASURE
    // планировщик перезаписывает буферы. FFTW_ESTIMATE планирует быстро, без замеров.
    FftwPlan plan(fftw_plan_dft_r2c_1d(static_cast<int>(n), in, out, FFTW_ESTIMATE), &fftw_destroy_plan);
    if (!plan) {
        throw std::runtime_error("FFTW failed to create a plan");
    }

    std::copy(x.begin(), x.end(), in);
    fftw_execute(plan.get());

    std::vector<Complex> bins(outSize);
    for (std::size_t k = 0; k < outSize; ++k) {
        bins[k] = Complex(out[k][0], out[k][1]);
    }
    return bins;
}

} // namespace dsp
