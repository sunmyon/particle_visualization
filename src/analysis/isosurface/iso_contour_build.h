#pragma once

#include <cstddef>

struct Mesh;
struct ParticleBlock;
enum class QuantityId : int;

#ifdef ISO_CONTOUR
struct IsoContourBuildParams {
  QuantityId selectedQuantity;
  float isoLevel = 0.0f;
  int maxTreeLevel = 15;
  std::size_t minParticles = 8;
  bool useVTK = true;
  bool verbose = false;
};

Mesh BuildIsoContourMesh(const ParticleBlock& particles,
                         const IsoContourBuildParams& params);
#endif
