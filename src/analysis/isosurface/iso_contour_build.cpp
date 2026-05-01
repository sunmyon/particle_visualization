#include "analysis/isosurface/iso_contour_build.h"

#include "data/particle_coordinates.h"

#ifdef ISO_CONTOUR
#include <cfloat>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <glm/glm.hpp>

#include "analysis/isosurface/isosurface_generator.h"
#include "analysis/isosurface/marching_cubes.h"
#include "core/quantity.h"
#include "data/particle_block.h"
#include "data/spatial/particle_octree.h"
#include "volume/adaptive_volume_tree.h"

namespace {

IsoContourMeshStats MeasureMeshStats(const Mesh& mesh, double seconds)
{
  IsoContourMeshStats stats;
  stats.vertices = mesh.vertices.size() / 3;
  stats.triangles = mesh.indices.size() / 3;
  stats.seconds = seconds;
  return stats;
}

std::string FormatStats(const char* label, const IsoContourMeshStats& stats)
{
  char buffer[256];
  std::snprintf(buffer,
                sizeof(buffer),
                "%s: %.3fs, %zu vertices, %zu triangles",
                label,
                stats.seconds,
                stats.vertices,
                stats.triangles);
  return buffer;
}

std::vector<ParticleDataForTree> MakeIsoParticles(const ParticleBlock& block,
                                                     QuantityId quantity)
{
  std::vector<ParticleDataForTree> particles;
  particles.reserve(block.particles.size());

  for (size_t ipart = 0; ipart < block.particles.size(); ++ipart) {
    const auto& pd = block.particles[ipart];
    float val = getScalarValue(block, pd, ipart, quantity);
    particles.push_back(
      {normalizedParticlePosition(pd, block.normalizedScale), val});
  }
  return particles;
}

BoundingBox ComputeIsoParticleBounds(
  const std::vector<ParticleDataForTree>& particles)
{
  BoundingBox worldBox;
  worldBox.min = glm::vec3(FLT_MAX);
  worldBox.max = glm::vec3(-FLT_MAX);
  for (const auto& p : particles) {
    worldBox.min = glm::min(worldBox.min, p.pos);
    worldBox.max = glm::max(worldBox.max, p.pos);
  }
  return worldBox;
}

IsoSurfaceParams MakeIsoSurfaceParams(const ParticleBlock& block,
                                      const IsoContourBuildParams& build)
{
  IsoSurfaceParams params;
  params.particles = MakeIsoParticles(block, build.selectedQuantity);
  params.worldBox = ComputeIsoParticleBounds(params.particles);
  params.isoLevel = build.isoLevel;
  params.minParticles = build.minParticles;
  params.maxDepth = static_cast<size_t>(std::max(1, build.maxTreeLevel));
  params.cornerReconstructionMode =
    std::clamp(build.cornerReconstructionMode, 0, 2);
  params.verbose = build.verbose;
  return params;
}

template <typename Fn>
Mesh BuildTimed(Fn&& fn, IsoContourMeshStats& stats)
{
  const auto start = std::chrono::steady_clock::now();
  Mesh mesh = fn();
  const auto stop = std::chrono::steady_clock::now();
  const double seconds =
    std::chrono::duration<double>(stop - start).count();
  stats = MeasureMeshStats(mesh, seconds);
  return mesh;
}

} // namespace

IsoContourBuildResult BuildIsoContourMeshDetailed(
  const ParticleBlock& block,
  const IsoContourBuildParams& build)
{
  IsoContourBuildResult result;
  if (block.particles.empty()) {
    result.message = "Iso-contour: no particles are loaded.";
    return result;
  }

  result.mesh = BuildTimed(
    [&] {
      return IsoSurfaceGenerator::generateMC(
        MakeIsoSurfaceParams(block, build));
    },
    result.manual);
  result.message = FormatStats("Adaptive MC", result.manual);
  return result;
}

IsoContourTreeBuildResult BuildAdaptiveIsoContourTree(
  const ParticleBlock& block,
  const IsoContourBuildParams& build)
{
  IsoContourTreeBuildResult result;
  IsoSurfaceParams params = MakeIsoSurfaceParams(block, build);

  const auto octreeStart = std::chrono::steady_clock::now();
  ParticleOctree octree(std::move(params.particles),
                        params.worldBox,
                        params.minParticles,
                        params.maxDepth);
  const auto octreeStop = std::chrono::steady_clock::now();
  result.octreeSeconds =
    std::chrono::duration<double>(octreeStop - octreeStart).count();
  std::cout << "[IsoContour] ParticleOctree built in "
            << result.octreeSeconds
            << " s"
            << " minParticles=" << params.minParticles
            << " maxDepth=" << params.maxDepth
            << "\n";

  AdaptiveVolumeTreeBuildParams treeParams;
  treeParams.minParticlesPerLeaf = params.minParticles;
  treeParams.maxDepth = params.maxDepth;
  treeParams.balanceTree = false;
  treeParams.expandBoundsByHsml = false;
  treeParams.emptySigmaEpsilon = 0.0f;
  treeParams.cornerReconstructionMode =
    std::clamp(params.cornerReconstructionMode, 0, 2);

  const auto adaptiveStart = std::chrono::steady_clock::now();
  AdaptiveVolumeTreeBuildResult built = BuildAdaptiveVolumeTreeFromOctree(
    octree,
    treeParams,
    [](float value) {
      return std::isfinite(value) ? value : 0.0f;
    });
  const auto adaptiveStop = std::chrono::steady_clock::now();
  result.adaptiveTreeSeconds =
    std::chrono::duration<double>(adaptiveStop - adaptiveStart).count();
  result.tree = std::move(built.tree);
  result.stats = built.stats;
  std::cout << "[IsoContour] Adaptive tree built in "
            << result.adaptiveTreeSeconds
            << " s"
            << " nodes=" << result.stats.nodeCount
            << " leaves=" << result.stats.leafCount
            << " droppedEmpty=" << result.stats.emptyNodesDropped
            << " reconstruction=" << treeParams.cornerReconstructionMode
            << "\n";

  char buffer[256];
  std::snprintf(buffer,
                sizeof(buffer),
                "Adaptive tree: octree %.3fs, tree %.3fs, %zu nodes, %zu leaves",
                result.octreeSeconds,
                result.adaptiveTreeSeconds,
                result.stats.nodeCount,
                result.stats.leafCount);
  result.message = buffer;
  return result;
}

IsoContourBuildResult ExtractAdaptiveIsoContourMesh(
  const AdaptiveVolumeTree& tree,
  float isoLevel)
{
  IsoContourBuildResult result;
  result.mesh = BuildTimed(
    [&] {
      return MarchingCubes::buildIsoSurface(tree, isoLevel);
    },
    result.manual);
  std::cout << "[IsoContour] Marching cubes built in "
            << result.manual.seconds
            << " s"
            << " vertices=" << result.manual.vertices
            << " triangles=" << result.manual.triangles
            << "\n";
  result.message = FormatStats("Adaptive MC", result.manual);
  return result;
}

Mesh BuildIsoContourMesh(const ParticleBlock& block,
                         const IsoContourBuildParams& build)
{
  return BuildIsoContourMeshDetailed(block, build).mesh;
}

#endif
