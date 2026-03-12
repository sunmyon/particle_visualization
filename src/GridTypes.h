
#pragma once

#include <array>
#include <cstddef>

namespace grid_analysis{
  struct Region3D {
    std::array<double, 3> xmin{0.0, 0.0, 0.0};
    std::array<double, 3> xmax{1.0, 1.0, 1.0};

    [[nodiscard]] std::array<double, 3> lengths() const {
      return {xmax[0] - xmin[0], xmax[1] - xmin[1], xmax[2] - xmin[2]};
    }
  };

  struct GridSize3D {
    int nx = 0;
    int ny = 0;
    int nz = 0;

    [[nodiscard]] std::size_t realSize() const {
      return static_cast<std::size_t>(nx) *
	static_cast<std::size_t>(ny) *
	static_cast<std::size_t>(nz);
    }

    [[nodiscard]] std::size_t complexSize() const {
      return static_cast<std::size_t>(nx) *
	static_cast<std::size_t>(ny) *
	static_cast<std::size_t>(nz / 2 + 1);
    }

    [[nodiscard]] bool valid() const {
      return nx > 0 && ny > 0 && nz > 0;
    }
  };  
}
