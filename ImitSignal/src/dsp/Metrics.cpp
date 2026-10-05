#include "dsp/Metrics.h"

#include <cmath>
#include <stdexcept>

namespace dsp {

double rmsDifference(const std::vector<double>& x, const std::vector<double>& ref, std::size_t margin)
{
    if (x.size() != ref.size()) {
        throw std::invalid_argument("Signals have different sizes");
    }
    if (x.empty()) {
        return 0.0;
    }
    if (2 * margin >= x.size()) {
        margin = 0;
    }

    double sum = 0.0;
    const std::size_t end = x.size() - margin;
    for (std::size_t i = margin; i < end; ++i) {
        const double d = x[i] - ref[i];
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(end - margin));
}

} // namespace dsp
