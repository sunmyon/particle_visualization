#include "analysis/power_spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <sstream>
#include <vector>

#include "FourierAnalysis.h"
#include "RegularGridFieldBuilder.h"
#include "data/sample_coordinates.h"

namespace {

grid_analysis::Region3D MakeGasRegion(const SimulationBlock& block,
                                       const PowerSpectrumParams& params,
                                       bool& valid)
{
  grid_analysis::Region3D region;
  if (params.useRegionBox) {
    valid = false;
    const double side = static_cast<double>(params.regionSideLength);
    if (!std::isfinite(side) || side <= 0.0) {
      return region;
    }
    const double half = 0.5 * side;
    for (int axis = 0; axis < 3; ++axis) {
      const double c = static_cast<double>(params.regionCenter[axis]);
      if (!std::isfinite(c)) {
        return region;
      }
      region.xmin[axis] = c - half;
      region.xmax[axis] = c + half;
    }
    valid = true;
    return region;
  }

  for (int axis = 0; axis < 3; ++axis) {
    region.xmin[axis] = std::numeric_limits<double>::max();
    region.xmax[axis] = std::numeric_limits<double>::lowest();
  }

  valid = false;
  for (const auto& p : block.particles) {
    if (p.type != 0) continue;
    valid = true;
    for (int axis = 0; axis < 3; ++axis) {
      const double x = static_cast<double>(p.position[axis]);
      region.xmin[axis] = std::min(region.xmin[axis], x);
      region.xmax[axis] = std::max(region.xmax[axis], x);
    }
  }

  if (!valid) return region;

  std::array<double, 3> center{};
  double maxLen = 0.0;
  for (int axis = 0; axis < 3; ++axis) {
    center[axis] = 0.5 * (region.xmin[axis] + region.xmax[axis]);
    maxLen = std::max(maxLen, region.xmax[axis] - region.xmin[axis]);
  }

  if (maxLen <= 0.0) {
    valid = false;
    return region;
  }

  const double half = 0.5 * maxLen;
  for (int axis = 0; axis < 3; ++axis) {
    region.xmin[axis] = center[axis] - half;
    region.xmax[axis] = center[axis] + half;
  }
  return region;
}

void SubtractMean(std::vector<double>& values)
{
  if (values.empty()) return;
  double sum = 0.0;
  for (double v : values) sum += v;
  const double mean = sum / static_cast<double>(values.size());
  for (double& v : values) v -= mean;
}

enum class AxisComponent {
  Axial,
  Radial,
  Toroidal,
  Poloidal
};

std::array<double, 3> NormalizeAxis(const float axisIn[3])
{
  std::array<double, 3> axis{
    static_cast<double>(axisIn[0]),
    static_cast<double>(axisIn[1]),
    static_cast<double>(axisIn[2])
  };
  const double n2 =
    axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
  if (!std::isfinite(n2) || n2 <= 0.0) {
    return {0.0, 0.0, 1.0};
  }
  const double inv = 1.0 / std::sqrt(n2);
  axis[0] *= inv;
  axis[1] *= inv;
  axis[2] *= inv;
  return axis;
}

std::array<double, 3> SpectrumCenter(const grid_analysis::Region3D& region,
                                     const PowerSpectrumParams& params)
{
  if (params.useRegionBox) {
    return {static_cast<double>(params.regionCenter[0]),
            static_cast<double>(params.regionCenter[1]),
            static_cast<double>(params.regionCenter[2])};
  }
  return {0.5 * (region.xmin[0] + region.xmax[0]),
          0.5 * (region.xmin[1] + region.xmax[1]),
          0.5 * (region.xmin[2] + region.xmax[2])};
}

void FillAxisComponentField(const std::vector<double>& fx,
                            const std::vector<double>& fy,
                            const std::vector<double>& fz,
                            const grid_analysis::Region3D& region,
                            const grid_analysis::GridSize3D& size,
                            const std::array<double, 3>& center,
                            const std::array<double, 3>& axis,
                            AxisComponent component,
                            std::vector<double>& outX,
                            std::vector<double>& outY,
                            std::vector<double>& outZ)
{
  const std::size_t n = size.realSize();
  outX.assign(n, 0.0);
  outY.assign(n, 0.0);
  outZ.assign(n, 0.0);

  const auto len = region.lengths();
  const double dx[3] = {
    len[0] / static_cast<double>(size.nx),
    len[1] / static_cast<double>(size.ny),
    len[2] / static_cast<double>(size.nz)
  };
  const double eps2 = 1.0e-24 *
    (len[0] * len[0] + len[1] * len[1] + len[2] * len[2]);

  for (int ix = 0; ix < size.nx; ++ix) {
    const double x = region.xmin[0] + (static_cast<double>(ix) + 0.5) * dx[0];
    for (int iy = 0; iy < size.ny; ++iy) {
      const double y = region.xmin[1] + (static_cast<double>(iy) + 0.5) * dx[1];
      for (int iz = 0; iz < size.nz; ++iz) {
        const double z = region.xmin[2] + (static_cast<double>(iz) + 0.5) * dx[2];
        const std::size_t idx =
          (static_cast<std::size_t>(ix) * size.ny + static_cast<std::size_t>(iy)) *
          static_cast<std::size_t>(size.nz) + static_cast<std::size_t>(iz);

        const std::array<double, 3> v{fx[idx], fy[idx], fz[idx]};
        const double vAxis =
          v[0] * axis[0] + v[1] * axis[1] + v[2] * axis[2];
        const std::array<double, 3> axial{
          vAxis * axis[0],
          vAxis * axis[1],
          vAxis * axis[2]
        };

        const std::array<double, 3> r{
          x - center[0],
          y - center[1],
          z - center[2]
        };
        const double rAxis =
          r[0] * axis[0] + r[1] * axis[1] + r[2] * axis[2];
        std::array<double, 3> er{
          r[0] - rAxis * axis[0],
          r[1] - rAxis * axis[1],
          r[2] - rAxis * axis[2]
        };
        const double rperp2 =
          er[0] * er[0] + er[1] * er[1] + er[2] * er[2];

        std::array<double, 3> radial{0.0, 0.0, 0.0};
        std::array<double, 3> toroidal{0.0, 0.0, 0.0};
        if (rperp2 > eps2) {
          const double invR = 1.0 / std::sqrt(rperp2);
          er[0] *= invR;
          er[1] *= invR;
          er[2] *= invR;

          const std::array<double, 3> ephi{
            axis[1] * er[2] - axis[2] * er[1],
            axis[2] * er[0] - axis[0] * er[2],
            axis[0] * er[1] - axis[1] * er[0]
          };
          const double vRadial = v[0] * er[0] + v[1] * er[1] + v[2] * er[2];
          const double vToroidal =
            v[0] * ephi[0] + v[1] * ephi[1] + v[2] * ephi[2];
          radial = {vRadial * er[0], vRadial * er[1], vRadial * er[2]};
          toroidal = {
            vToroidal * ephi[0],
            vToroidal * ephi[1],
            vToroidal * ephi[2]
          };
        }

        std::array<double, 3> out{0.0, 0.0, 0.0};
        switch (component) {
        case AxisComponent::Axial:
          out = axial;
          break;
        case AxisComponent::Radial:
          out = radial;
          break;
        case AxisComponent::Toroidal:
          out = toroidal;
          break;
        case AxisComponent::Poloidal:
          out = {
            axial[0] + radial[0],
            axial[1] + radial[1],
            axial[2] + radial[2]
          };
          break;
        }

        outX[idx] = out[0];
        outY[idx] = out[1];
        outZ[idx] = out[2];
      }
    }
  }
}

std::vector<double> ComputeComponentPower(
  fourier_analysis::FourierGrid3D& fourier,
  const fourier_analysis::PowerSpectrumAnalyzer& analyzer,
  const std::vector<double>& fx,
  const std::vector<double>& fy,
  const std::vector<double>& fz,
  const grid_analysis::Region3D& region,
  const grid_analysis::GridSize3D& size,
  const std::array<double, 3>& center,
  const std::array<double, 3>& axis,
  AxisComponent component)
{
  std::vector<double> cx, cy, cz;
  FillAxisComponentField(fx, fy, fz, region, size, center, axis,
                         component, cx, cy, cz);
  fourier.setVectorField(cx, cy, cz);
  fourier.forwardVectorFFT();
  return analyzer.computeVectorSpectrum(fourier).powerTotal;
}

}  // namespace

PowerSpectrumResult
PowerSpectrumComputer::compute(const SimulationBlock& block,
                               const PowerSpectrumParams& params) const
{
  PowerSpectrumResult result;
  result.gridSize = params.gridSize;
  result.vectorSpectrum = (params.fieldKind != 0);
  result.fieldLabel = result.vectorSpectrum
    ? (params.vectorField == 1 ? "B field" : "velocity")
    : QuantityLabel(params.scalarQuantity);

  const int n = std::clamp(params.gridSize, 8, 256);
  if (result.vectorSpectrum &&
      params.vectorField == 1 &&
      !block.hasSoAAs(soa_views::Bfield)) {
    result.message = "B field power spectrum requested, but this snapshot has no Bfield data.";
    return result;
  }

  bool regionValid = false;
  const grid_analysis::Region3D region = MakeGasRegion(block, params, regionValid);
  if (!regionValid) {
    result.message = params.useRegionBox
      ? "Invalid power-spectrum region box."
      : "No gas cells are available for power spectrum.";
    return result;
  }

  try {
    grid_analysis::RegularGridFieldBuilder builder;
    builder.setRegion(region);
    builder.setGridSize({n, n, n});
    builder.setDepositScheme(grid_analysis::RegularGridFieldBuilder::DepositScheme::CIC);

    int deposited = 0;
    for (std::size_t i = 0; i < block.particles.size(); ++i) {
      const auto& p = block.particles[i];
      if (p.type != 0) continue;

      std::array<double, 3> pos{
        static_cast<double>(p.position[0]),
        static_cast<double>(p.position[1]),
        static_cast<double>(p.position[2])
      };

      if (result.vectorSpectrum) {
        std::array<double, 3> vec{};
        if (params.vectorField == 1) {
          float b[3] = {0.0f, 0.0f, 0.0f};
          if (!block.readSoAAs(soa_views::Bfield, i, b)) continue;
          vec = {static_cast<double>(b[0]),
                 static_cast<double>(b[1]),
                 static_cast<double>(b[2])};
        } else {
          vec = {static_cast<double>(p.vel[0]),
                 static_cast<double>(p.vel[1]),
                 static_cast<double>(p.vel[2])};
        }
        builder.depositVectorSample(pos, vec, 1.0);
      } else {
        const double value = static_cast<double>(
          getScalarValue(block, p, static_cast<int>(i), params.scalarQuantity));
        if (!std::isfinite(value)) continue;
        builder.depositScalarSample(pos, value, 1.0);
      }
      ++deposited;
    }
    if (deposited <= 0) {
      result.message = params.useRegionBox
        ? "No gas cells are inside the selected power-spectrum region."
        : "No gas cells are available for power spectrum.";
      return result;
    }
    builder.normalize();

    const grid_analysis::GridSize3D gridSize{n, n, n};
    fourier_analysis::FourierGrid3D fourier(region, gridSize);
    const fourier_analysis::PowerSpectrumAnalyzer analyzer;
    if (result.vectorSpectrum) {
      std::vector<double> fx, fy, fz;
      builder.getVectorField(fx, fy, fz);
      if (params.subtractMean) {
        SubtractMean(fx);
        SubtractMean(fy);
        SubtractMean(fz);
      }

      fourier.setVectorField(fx, fy, fz);
      fourier.forwardVectorFFT();

      const auto spectrum = analyzer.computeVectorSpectrum(fourier);
      result.k = spectrum.kCenter;
      result.powerTotal = spectrum.powerTotal;
      result.powerSolenoidal = spectrum.powerSolenoidal;
      result.powerCompressive = spectrum.powerCompressive;
      result.nModes = spectrum.nModes;

      const std::array<double, 3> center = SpectrumCenter(region, params);
      const std::array<double, 3> axis = NormalizeAxis(params.analysisAxis);
      result.powerAxial =
        ComputeComponentPower(fourier, analyzer, fx, fy, fz, region, gridSize,
                              center, axis, AxisComponent::Axial);
      result.powerRadial =
        ComputeComponentPower(fourier, analyzer, fx, fy, fz, region, gridSize,
                              center, axis, AxisComponent::Radial);
      result.powerToroidal =
        ComputeComponentPower(fourier, analyzer, fx, fy, fz, region, gridSize,
                              center, axis, AxisComponent::Toroidal);
      result.powerPoloidal =
        ComputeComponentPower(fourier, analyzer, fx, fy, fz, region, gridSize,
                              center, axis, AxisComponent::Poloidal);
    } else {
      std::vector<double> scalar;
      builder.getScalarField(scalar);
      if (params.subtractMean) {
        SubtractMean(scalar);
      }

      fourier.setScalarField(scalar);
      fourier.forwardScalarFFT();

      const auto spectrum = analyzer.computeScalarSpectrum(fourier);
      result.k = spectrum.kCenter;
      result.powerTotal = spectrum.power;
      result.powerSolenoidal.clear();
      result.powerCompressive.clear();
      result.powerAxial.clear();
      result.powerRadial.clear();
      result.powerToroidal.clear();
      result.powerPoloidal.clear();
      result.nModes = spectrum.nModes;
    }
    result.depositedSamples = deposited;
    result.gridSize = n;
    result.valid = true;
    result.success = true;

    std::ostringstream msg;
    msg << result.fieldLabel << " power spectrum computed (grid=" << n
        << "^3, samples=" << deposited << ").";
    result.message = msg.str();
  } catch (const std::exception& e) {
    result.message = e.what();
  }

  return result;
}
