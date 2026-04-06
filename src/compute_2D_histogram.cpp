#include "compute_2D_histogram.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>

Histogram2DResult
Histogram2DComputer::compute(const ParticleBlock& partblock,
                             const Histogram2DParams& params,
                             const Histogram2DContext& ctx) const
{
  Histogram2DResult result;
  result.valid = false;

  if (params.bins1 <= 0 || params.bins2 <= 0) {
    return result;
  }

  std::function<bool(const ParticleData&)> condition =
    [](const ParticleData&) { return true; };

#ifdef USE_CONVEX_HULL
  if (params.useConvexHull) {
    condition = [&ctx](const ParticleData& p) -> bool {
      if (!ctx.convexHulls || ctx.convexHulls->empty()) {
        return false;
      }

      const std::array<double, 3> pt = { p.pos[0], p.pos[1], p.pos[2] };

      for (const auto& hull : *ctx.convexHulls) {
        if (hull && hull->isInside(pt)) {
          return true;
        }
      }
      return false;
    };
  }
#endif

  if (params.useCameraCenter) {
    auto isWithinRadius = [&ctx, &params](const ParticleData& p) -> bool {
      if (!ctx.cameraCenter) {
        return false;
      }

      glm::vec3 pos(p.pos[0], p.pos[1], p.pos[2]);
      return glm::length(pos - *ctx.cameraCenter) <= params.cameraRadius;
    };

    auto prevFunc = condition;
    condition = [prevFunc, isWithinRadius](const ParticleData& p) -> bool {
      return prevFunc(p) && isWithinRadius(p);
    };
  }

  float min1 = 1.e30f, max1 = -1.e30f;
  float min2 = 1.e30f, max2 = -1.e30f;

  const float* centerPtr = ctx.cameraCenter ? &ctx.cameraCenter->x : nullptr;

  if (params.autoRange) {
    bool firstX = true;
    bool firstY = true;

    for (size_t ipart = 0; ipart < partblock.particles.size(); ipart++) {
      const ParticleData& p = partblock.particles[ipart];
      if (p.type != 0) continue;
      if (!condition(p)) continue;

      float v1 = getScalarValue(partblock, p, ipart, params.var1, centerPtr);
      float v2 = getScalarValue(partblock, p, ipart, params.var2, centerPtr);

      bool skipX = false;
      bool skipY = false;

      if (params.logScaleX) {
        if (v1 > 0.0f) v1 = std::log10(v1);
        else skipX = true;
      }

      if (params.logScaleY) {
        if (v2 > 0.0f) v2 = std::log10(v2);
        else skipY = true;
      }

      if (!skipX) {
        if (firstX) {
          min1 = max1 = v1;
          firstX = false;
        } else {
          if (v1 < min1) min1 = v1;
          if (v1 > max1) max1 = v1;
        }
      }

      if (!skipY) {
        if (firstY) {
          min2 = max2 = v2;
          firstY = false;
        } else {
          if (v2 < min2) min2 = v2;
          if (v2 > max2) max2 = v2;
        }
      }
    }

    if (firstX) {
      min1 = params.range1_min;
      max1 = params.range1_max;
    }
    if (firstY) {
      min2 = params.range2_min;
      max2 = params.range2_max;
    }
  } else {
    min1 = params.range1_min;
    max1 = params.range1_max;
    min2 = params.range2_min;
    max2 = params.range2_max;
  }

  TrackingVector<TrackingVector<float>> histogram(
    params.bins1, TrackingVector<float>(params.bins2, 0.0f));

  const float binSize1 = (max1 - min1) / params.bins1;
  const float binSize2 = (max2 - min2) / params.bins2;

  if (binSize1 <= 0.0f || binSize2 <= 0.0f) {
    result.range1_min = min1;
    result.range1_max = max1;
    result.range2_min = min2;
    result.range2_max = max2;
    return result;
  }

  for (size_t ipart = 0; ipart < partblock.particles.size(); ipart++) {
    const ParticleData& p = partblock.particles[ipart];
    if (p.type != 0) continue;
    if (!condition(p)) continue;

    float v1 = getScalarValue(partblock, p, ipart, params.var1, centerPtr);
    float v2 = getScalarValue(partblock, p, ipart, params.var2, centerPtr);

    if (params.logScaleX) {
      if (v1 > 0.0f) v1 = std::log10(v1);
      else continue;
    }

    if (params.logScaleY) {
      if (v2 > 0.0f) v2 = std::log10(v2);
      else continue;
    }

    if (v1 < min1 || v1 >= max1 || v2 < min2 || v2 >= max2)
      continue;

    int binIndex1 = std::min(params.bins1 - 1, static_cast<int>((v1 - min1) / binSize1));
    int binIndex2 = std::min(params.bins2 - 1, static_cast<int>((v2 - min2) / binSize2));

    histogram[binIndex1][binIndex2] += p.mass;
  }

  TrackingVector<float> centers1(params.bins1, 0.0f);
  for (int i = 0; i < params.bins1; i++) {
    centers1[i] = min1 + (i + 0.5f) * binSize1;
    if (params.logScaleX)
      centers1[i] = std::pow(10.0f, min1 + (i + 0.5f) * binSize1);
  }

  TrackingVector<float> centers2(params.bins2, 0.0f);
  for (int j = 0; j < params.bins2; j++) {
    centers2[j] = min2 + (j + 0.5f) * binSize2;
    if (params.logScaleY)
      centers2[j] = std::pow(10.0f, min2 + (j + 0.5f) * binSize2);
  }

  if (params.logScaleColor) {
    float min_val = 1.e30f;
    for (int i = 0; i < params.bins1; i++) {
      for (int j = 0; j < params.bins2; j++) {
        if (histogram[i][j] > 0.0f)
          min_val = std::min(min_val, histogram[i][j]);
      }
    }

    if (min_val < 1.e29f) {
      for (int i = 0; i < params.bins1; i++) {
        for (int j = 0; j < params.bins2; j++) {
          if (histogram[i][j] == 0.0f)
            histogram[i][j] = 0.1f * min_val;
          histogram[i][j] = std::log10(histogram[i][j]);
        }
      }
    }
  }

  result.centers1 = std::move(centers1);
  result.centers2 = std::move(centers2);
  result.values   = std::move(histogram);
  result.range1_min = min1;
  result.range1_max = max1;
  result.range2_min = min2;
  result.range2_max = max2;
  result.valid = true;

  return result;
}
