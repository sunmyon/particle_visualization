#include "analysis/isosurface/isosurface_generator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

#include "analysis/isosurface/marching_cubes.h"
#include "data/spatial/particle_octree.h"
#include "volume/adaptive_volume_tree.h"

Mesh IsoSurfaceGenerator::generateMC(IsoSurfaceParams params)
{
  const auto totalStart = std::chrono::steady_clock::now();
  const auto octreeStart = std::chrono::steady_clock::now();
  ParticleOctree octree(std::move(params.particles),
                        params.worldBox,
                        params.minParticles,
                        params.maxDepth);
  const auto octreeStop = std::chrono::steady_clock::now();
  std::cout << "[IsoContour] ParticleOctree built in "
            << std::chrono::duration<double>(octreeStop - octreeStart).count()
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
  std::cout << "[IsoContour] Adaptive tree built in "
            << std::chrono::duration<double>(adaptiveStop - adaptiveStart).count()
            << " s"
            << " nodes=" << built.stats.nodeCount
            << " leaves=" << built.stats.leafCount
            << " droppedEmpty=" << built.stats.emptyNodesDropped
            << " reconstruction=" << treeParams.cornerReconstructionMode
            << "\n";

  const auto meshStart = std::chrono::steady_clock::now();
  Mesh mesh = MarchingCubes::buildIsoSurface(built.tree, params.isoLevel);
  const auto meshStop = std::chrono::steady_clock::now();
  const auto totalStop = std::chrono::steady_clock::now();
  std::cout << "[IsoContour] Marching cubes built in "
            << std::chrono::duration<double>(meshStop - meshStart).count()
            << " s"
            << " vertices=" << mesh.vertices.size() / 3
            << " triangles=" << mesh.indices.size() / 3
            << "\n";
  std::cout << "[IsoContour] Adaptive MC total "
            << std::chrono::duration<double>(totalStop - totalStart).count()
            << " s\n";

  return mesh;
}
