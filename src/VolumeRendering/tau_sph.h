#pragma once
#include <vector>

namespace sphlut {

// Kernel type.
enum class Kernel { CubicSpline /*, WendlandC2, ... */ };

struct TauLUT {
    std::vector<float> data; // J(q) on [0, R]
    float R;                 // support (e.g., 2.0)
    float alpha3;            // 3D normalize (e.g., 1/pi)
};

// samples: 1D texture resolution.
TauLUT buildTauLUT(Kernel k, int samples = 512);

} // namespace sphlut
