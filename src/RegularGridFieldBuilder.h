
#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include "GridTypes.h"

namespace grid_analysis {
  class RegularGridFieldBuilder {
  public:
    enum class DepositScheme {
      NGP,
      CIC
    };

    enum class FieldKind {
      None,
      Scalar,
      Vector3
    };

    RegularGridFieldBuilder();

    void setRegion(const Region3D& region);
    void setGridSize(const GridSize3D& size);
    void setDepositScheme(DepositScheme scheme);

    void clear();
    void resetGrid();

    void depositScalarSample(const std::array<double, 3>& pos,
			     double value,
			     double weight = 1.0);

    void depositVectorSample(const std::array<double, 3>& pos,
			     const std::array<double, 3>& vec,
			     double weight = 1.0);

    void normalize();

    [[nodiscard]] FieldKind getFieldKind() const;

    void getScalarField(std::vector<double>& scalar) const;
    void getVectorField(std::vector<double>& fx,
			std::vector<double>& fy,
			std::vector<double>& fz) const;

  private:
    bool isInside(const std::array<double, 3>& pos) const;
    std::size_t flattenIndex(int ix, int iy, int iz) const;

    void ensureScalarStorage();
    void ensureVectorStorage();

    void depositScalarNGP(const std::array<double, 3>& pos,
			  double value,
			  double weight);
    void depositScalarCIC(const std::array<double, 3>& pos,
			  double value,
			  double weight);

    void depositVectorNGP(const std::array<double, 3>& pos,
			  const std::array<double, 3>& vec,
			  double weight);
    void depositVectorCIC(const std::array<double, 3>& pos,
			  const std::array<double, 3>& vec,
			  double weight);

    Region3D region_;
    GridSize3D size_;
    DepositScheme scheme_ = DepositScheme::CIC;
    FieldKind fieldKind_ = FieldKind::None;

    std::vector<double> scalar_;
    std::vector<double> vx_;
    std::vector<double> vy_;
    std::vector<double> vz_;
    std::vector<double> weight_;
  };
}  // namespace grid_analysis
