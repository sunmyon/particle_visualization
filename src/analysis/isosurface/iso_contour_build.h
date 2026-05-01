#pragma once

#include <cstddef>
#include <string>

struct ParticleBlock;
enum class QuantityId : int;

#ifdef ISO_CONTOUR
#include "analysis/isosurface/mesh_data.h"
#include "volume/adaptive_volume_tree.h"

struct IsoContourBuildParams {
  QuantityId selectedQuantity;
  float isoLevel = 0.0f;
  int maxTreeLevel = 15;
  std::size_t minParticles = 64;
  int cornerReconstructionMode = 1; // 0=cell average, 1=shared corners, 2=face gradient.
  bool verbose = false;
};

struct IsoContourMeshStats {
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  double seconds = 0.0;
};

struct IsoContourBuildResult {
  Mesh mesh;
  IsoContourMeshStats manual;
  std::string message;
};

struct IsoContourTreeBuildResult {
  AdaptiveVolumeTree tree;
  AdaptiveVolumeTreeStats stats;
  double octreeSeconds = 0.0;
  double adaptiveTreeSeconds = 0.0;
  std::string message;
};

Mesh BuildIsoContourMesh(const ParticleBlock& particles,
                         const IsoContourBuildParams& params);
IsoContourBuildResult BuildIsoContourMeshDetailed(
  const ParticleBlock& particles,
  const IsoContourBuildParams& params);
IsoContourTreeBuildResult BuildAdaptiveIsoContourTree(
  const ParticleBlock& particles,
  const IsoContourBuildParams& params);
IsoContourBuildResult ExtractAdaptiveIsoContourMesh(
  const AdaptiveVolumeTree& tree,
  float isoLevel);
#endif
