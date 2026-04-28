//=== nanoflann_sph.cpp ===
#include "analysis/isosurface/density_evaluate.h"
#include <glm/gtc/constants.hpp>
#include <nanoflann.hpp>

// Cubic-spline kernel for SPH.
static float cubicSplineKernel(float r, float h) {
    const float q = r / h;
    const float sigma = 1.0f / (glm::pi<float>() * h*h*h);
    if (q < 1.0f) {
        return sigma * (1.0f - 1.5f*q*q + 0.75f*q*q*q);
    } else if (q < 2.0f) {
        float t = 2.0f - q;
        return sigma * 0.25f * t*t*t;
    } else {
        return 0.0f;
    }
}

SPHInterpolator::SPHInterpolator(TrackingVector<ParticleDataForKdTree>&& pts)
  : cloud_{std::move(pts)},
    index_(3 /*dim*/, cloud_,
           nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */))
{
    index_.buildIndex();
}


float SPHInterpolator::sampleValue(const glm::vec3& pos, size_t K, float alpha) const {
    // 1) KNN search.
    std::vector<size_t>   retIdx(K);
    std::vector<float>    outDistSqr(K);
    nanoflann::KNNResultSet<float> resultSet(K);
    resultSet.init(&retIdx[0], &outDistSqr[0]);
    float query[3] = {pos.x, pos.y, pos.z};

    nanoflann::SearchParameters params;      // No template argument required.
    index_.findNeighbors(resultSet, query, params);
    
    // 2) Smoothing length h = alpha * sqrt(max distance).
    float rK = 0;
    for (size_t i = 0; i < K; ++i) {
      rK = std::max(rK, std::sqrt(outDistSqr[i]));
    }
    float h = alpha * rK;
    if (h < 1e-6f) h = 1e-6f;

    // 3) Normalized SPH interpolation.
    float sumW  = 0;
    float sumWV = 0;
    for (size_t i = 0; i < K; ++i) {
      const auto& pd = cloud_.particles[retIdx[i]];
      float r = std::sqrt(outDistSqr[i]);
      float w = cubicSplineKernel(r, h);
      sumW  += w;
      sumWV += pd.val * w;
    }
    
    return (sumW > 1e-8f ? sumWV / sumW : 0.0f);
}


float SPHInterpolator::sample(const glm::vec3& pos) const {
#ifdef VORONOI_ESTIMATE
  return sampleValue(pos, /*K=*/1, /*alpha=*/1.2f);
#else
  return sampleValue(pos, /*K=*/32, /*alpha=*/1.2f);
  //return sampleValue(pos, /*K=*/1, /*alpha=*/1.2f);
#endif
}

 
