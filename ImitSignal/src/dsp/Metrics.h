#pragma once

#include <cstddef>
#include <vector>

namespace dsp {

// Среднеквадратичное отклонение x от эталона ref: sqrt(mean((x - ref)^2)).
// margin отсчётов с каждого края пропускаются, чтобы краевые эффекты фильтра
// не портили оценку. Если края съели бы весь сигнал, считаем по всему.
double rmsDifference(const std::vector<double>& x, const std::vector<double>& ref, std::size_t margin = 0);

} // namespace dsp
