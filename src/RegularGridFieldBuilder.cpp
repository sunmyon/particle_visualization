
#include "RegularGridFieldBuilder.h"
#include "FourierAnalysis.h"

#include <algorithm>
#include <cmath>

namespace grid_analysis {
  RegularGridFieldBuilder::RegularGridFieldBuilder() = default;

  void RegularGridFieldBuilder::setRegion(const Region3D& region) {
    region_ = region;
  }

  void RegularGridFieldBuilder::setGridSize(const GridSize3D& size) {
    if (!size.valid()) {
      throw std::runtime_error("RegularGridFieldBuilder: invalid grid size.");
    }
    size_ = size;
    resetGrid();
  }

  void RegularGridFieldBuilder::setDepositScheme(const DepositScheme scheme) {
    scheme_ = scheme;
  }

  void RegularGridFieldBuilder::clear() {
    fieldKind_ = FieldKind::None;
    scalar_.clear();
    vx_.clear();
    vy_.clear();
    vz_.clear();
    weight_.clear();
  }

  void RegularGridFieldBuilder::resetGrid() {
    const std::size_t n = size_.realSize();
    weight_.assign(n, 0.0);
    if (fieldKind_ == FieldKind::Scalar) {
      scalar_.assign(n, 0.0);
    } else if (fieldKind_ == FieldKind::Vector3) {
      vx_.assign(n, 0.0);
      vy_.assign(n, 0.0);
      vz_.assign(n, 0.0);
    }
  }

  void RegularGridFieldBuilder::depositScalarSample(const std::array<double, 3>& pos,
						    const double value,
						    const double weight) {
    if (!size_.valid()) {
      throw std::runtime_error("RegularGridFieldBuilder: grid size is not configured.");
    }
    if (!isInside(pos)) return;
    ensureScalarStorage();

    switch (scheme_) {
    case DepositScheme::NGP:
      depositScalarNGP(pos, value, weight);
      break;
    case DepositScheme::CIC:
      depositScalarCIC(pos, value, weight);
      break;
    }
  }

  void RegularGridFieldBuilder::depositVectorSample(const std::array<double, 3>& pos,
						    const std::array<double, 3>& vec,
						    const double weight) {
    if (!size_.valid()) {
      throw std::runtime_error("RegularGridFieldBuilder: grid size is not configured.");
    }
    if (!isInside(pos)) return;
    ensureVectorStorage();

    switch (scheme_) {
    case DepositScheme::NGP:
      depositVectorNGP(pos, vec, weight);
      break;
    case DepositScheme::CIC:
      depositVectorCIC(pos, vec, weight);
      break;
    }
  }

  void RegularGridFieldBuilder::normalize() {
    if (fieldKind_ == FieldKind::Scalar) {
      for (std::size_t i = 0; i < scalar_.size(); ++i) {
	if (weight_[i] > 0.0) scalar_[i] /= weight_[i];
      }
    } else if (fieldKind_ == FieldKind::Vector3) {
      for (std::size_t i = 0; i < vx_.size(); ++i) {
	if (weight_[i] > 0.0) {
	  vx_[i] /= weight_[i];
	  vy_[i] /= weight_[i];
	  vz_[i] /= weight_[i];
	}
      }
    }
  }

  RegularGridFieldBuilder::FieldKind RegularGridFieldBuilder::getFieldKind() const {
    return fieldKind_;
  }

  void RegularGridFieldBuilder::getScalarField(std::vector<double>& scalar) const {
    if (fieldKind_ != FieldKind::Scalar) {
      throw std::runtime_error("RegularGridFieldBuilder: scalar field is not available.");
    }
    scalar = scalar_;
  }

  void RegularGridFieldBuilder::getVectorField(std::vector<double>& fx,
					       std::vector<double>& fy,
					       std::vector<double>& fz) const {
    if (fieldKind_ != FieldKind::Vector3) {
      throw std::runtime_error("RegularGridFieldBuilder: vector field is not available.");
    }
    fx = vx_;
    fy = vy_;
    fz = vz_;
  }

  bool RegularGridFieldBuilder::isInside(const std::array<double, 3>& pos) const {
    return pos[0] >= region_.xmin[0] && pos[0] < region_.xmax[0] &&
      pos[1] >= region_.xmin[1] && pos[1] < region_.xmax[1] &&
      pos[2] >= region_.xmin[2] && pos[2] < region_.xmax[2];
  }

  std::size_t RegularGridFieldBuilder::flattenIndex(const int ix, const int iy, const int iz) const {
    return (static_cast<std::size_t>(ix) * size_.ny + static_cast<std::size_t>(iy)) *
      static_cast<std::size_t>(size_.nz) +
      static_cast<std::size_t>(iz);
  }

  void RegularGridFieldBuilder::ensureScalarStorage() {
    if (fieldKind_ == FieldKind::Vector3) {
      throw std::runtime_error("RegularGridFieldBuilder: cannot mix scalar and vector deposits.");
    }
    if (fieldKind_ == FieldKind::None) {
      fieldKind_ = FieldKind::Scalar;
      scalar_.assign(size_.realSize(), 0.0);
      weight_.assign(size_.realSize(), 0.0);
    }
  }

  void RegularGridFieldBuilder::ensureVectorStorage() {
    if (fieldKind_ == FieldKind::Scalar) {
      throw std::runtime_error("RegularGridFieldBuilder: cannot mix scalar and vector deposits.");
    }
    if (fieldKind_ == FieldKind::None) {
      fieldKind_ = FieldKind::Vector3;
      vx_.assign(size_.realSize(), 0.0);
      vy_.assign(size_.realSize(), 0.0);
      vz_.assign(size_.realSize(), 0.0);
      weight_.assign(size_.realSize(), 0.0);
    }
  }

  void RegularGridFieldBuilder::depositScalarNGP(const std::array<double, 3>& pos,
						 const double value,
						 const double weight) {
    const auto len = region_.lengths();
    const double gx = (pos[0] - region_.xmin[0]) / len[0] * static_cast<double>(size_.nx);
    const double gy = (pos[1] - region_.xmin[1]) / len[1] * static_cast<double>(size_.ny);
    const double gz = (pos[2] - region_.xmin[2]) / len[2] * static_cast<double>(size_.nz);

    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, size_.nx - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(gy)), 0, size_.ny - 1);
    const int iz = std::clamp(static_cast<int>(std::floor(gz)), 0, size_.nz - 1);

    const std::size_t idx = flattenIndex(ix, iy, iz);
    scalar_[idx] += weight * value;
    weight_[idx] += weight;
  }

  void RegularGridFieldBuilder::depositScalarCIC(const std::array<double, 3>& pos,
						 const double value,
						 const double weight) {
    const auto len = region_.lengths();

    const double gx = (pos[0] - region_.xmin[0]) / len[0] * static_cast<double>(size_.nx) - 0.5;
    const double gy = (pos[1] - region_.xmin[1]) / len[1] * static_cast<double>(size_.ny) - 0.5;
    const double gz = (pos[2] - region_.xmin[2]) / len[2] * static_cast<double>(size_.nz) - 0.5;

    const int ix0 = static_cast<int>(std::floor(gx));
    const int iy0 = static_cast<int>(std::floor(gy));
    const int iz0 = static_cast<int>(std::floor(gz));

    const double tx = gx - static_cast<double>(ix0);
    const double ty = gy - static_cast<double>(iy0);
    const double tz = gz - static_cast<double>(iz0);

    for (int dx = 0; dx <= 1; ++dx) {
      const int ix = ix0 + dx;
      if (ix < 0 || ix >= size_.nx) continue;
      const double wx = dx ? tx : (1.0 - tx);

      for (int dy = 0; dy <= 1; ++dy) {
	const int iy = iy0 + dy;
	if (iy < 0 || iy >= size_.ny) continue;
	const double wy = dy ? ty : (1.0 - ty);

	for (int dz = 0; dz <= 1; ++dz) {
	  const int iz = iz0 + dz;
	  if (iz < 0 || iz >= size_.nz) continue;
	  const double wz = dz ? tz : (1.0 - tz);

	  const double w = weight * wx * wy * wz;
	  const std::size_t idx = flattenIndex(ix, iy, iz);

	  scalar_[idx] += w * value;
	  weight_[idx] += w;
	}
      }
    }
  }

  void RegularGridFieldBuilder::depositVectorNGP(const std::array<double, 3>& pos,
						 const std::array<double, 3>& vec,
						 const double weight) {
    const auto len = region_.lengths();
    const double gx = (pos[0] - region_.xmin[0]) / len[0] * static_cast<double>(size_.nx);
    const double gy = (pos[1] - region_.xmin[1]) / len[1] * static_cast<double>(size_.ny);
    const double gz = (pos[2] - region_.xmin[2]) / len[2] * static_cast<double>(size_.nz);

    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, size_.nx - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(gy)), 0, size_.ny - 1);
    const int iz = std::clamp(static_cast<int>(std::floor(gz)), 0, size_.nz - 1);

    const std::size_t idx = flattenIndex(ix, iy, iz);
    vx_[idx] += weight * vec[0];
    vy_[idx] += weight * vec[1];
    vz_[idx] += weight * vec[2];
    weight_[idx] += weight;
  }

  void RegularGridFieldBuilder::depositVectorCIC(const std::array<double, 3>& pos,
						 const std::array<double, 3>& vec,
						 const double weight) {
    const auto len = region_.lengths();

    const double gx = (pos[0] - region_.xmin[0]) / len[0] * static_cast<double>(size_.nx) - 0.5;
    const double gy = (pos[1] - region_.xmin[1]) / len[1] * static_cast<double>(size_.ny) - 0.5;
    const double gz = (pos[2] - region_.xmin[2]) / len[2] * static_cast<double>(size_.nz) - 0.5;

    const int ix0 = static_cast<int>(std::floor(gx));
    const int iy0 = static_cast<int>(std::floor(gy));
    const int iz0 = static_cast<int>(std::floor(gz));

    const double tx = gx - static_cast<double>(ix0);
    const double ty = gy - static_cast<double>(iy0);
    const double tz = gz - static_cast<double>(iz0);

    for (int dx = 0; dx <= 1; ++dx) {
      const int ix = ix0 + dx;
      if (ix < 0 || ix >= size_.nx) continue;
      const double wx = dx ? tx : (1.0 - tx);

      for (int dy = 0; dy <= 1; ++dy) {
	const int iy = iy0 + dy;
	if (iy < 0 || iy >= size_.ny) continue;
	const double wy = dy ? ty : (1.0 - ty);

	for (int dz = 0; dz <= 1; ++dz) {
	  const int iz = iz0 + dz;
	  if (iz < 0 || iz >= size_.nz) continue;
	  const double wz = dz ? tz : (1.0 - tz);

	  const double w = weight * wx * wy * wz;
	  const std::size_t idx = flattenIndex(ix, iy, iz);

	  vx_[idx] += w * vec[0];
	  vy_[idx] += w * vec[1];
	  vz_[idx] += w * vec[2];
	  weight_[idx] += w;
	}
      }
    }
  }

}  // namespace fourier_analysis
