#include "app/app_render_sync.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

#include "app/state/app_state.h"
#include "app/state/render_runtime_state.h"
#include "data/simulation_dataset.h"
#include "interaction/camera.h"
#include "render/particle_visual_config.h"
#include "render/particle_lod.h"
#include "render/render_resources.h"
#include "render/render_system.h"

ParticleRenderInput MakeParticleRenderInput(const SimulationDataset& particles)
{
  return ParticleRenderInput{
    &particles.simulationBlock,
    &particles.flag_mask,
    &particles.flag_stress,
    particles.particlesDirty,
    particles.velocityDirty
  };
}

static void PropagateDirtyFlags(const ParticleRenderInput& input,
                                RenderSystem& rs)
{
  if (input.particlesDirty) {
    rs.build.particlesDirty = true;
  }

  if (input.velocityDirty) {
    rs.build.velocityInstancesDirty = true;
  }
}

static ParticleRenderBuildResult UpdateParticleRenderSceneData(const ParticleRenderInput& input,
                                                               const ParticleVisualConfig& particleVisual,
                                                               const CameraContext& camera,
                                                               double currentTime,
                                                               bool softwareRenderer,
                                                               const VelocityRenderState& velocityState,
                                                               const RenderSchedulingState& scheduling,
                                                               RenderSystem& rs)
{
  ParticleRenderBuildResult result;

  const bool lodTreeBuildSettingsChanged =
    rs.scene.particleLodSettings.minNodeParticles !=
      scheduling.particleLod.minNodeParticles ||
    rs.scene.particleLodSettings.maxDepth != scheduling.particleLod.maxDepth;

  const bool lodProxySettingsChanged =
    rs.scene.particleLodSettings.proxyFraction !=
      scheduling.particleLod.proxyFraction ||
    rs.scene.particleLodSettings.theta != scheduling.particleLod.theta ||
    rs.scene.particleLodSettings.screenPixelThreshold !=
      scheduling.particleLod.screenPixelThreshold ||
    rs.scene.particleLodSettings.focusUpdateDistance !=
      scheduling.particleLod.focusUpdateDistance ||
    rs.scene.particleLodSettings.proxyUpdateRateHz !=
      scheduling.particleLod.proxyUpdateRateHz;

  const float focusUpdateDistance =
    std::max(0.0f, scheduling.particleLod.focusUpdateDistance);
  const bool lodFocusMoved =
    glm::length(rs.scene.particleLodFocus - camera.cameraTarget) >
    focusUpdateDistance;
  const float proxyUpdateRateHz =
    std::max(0.0f, scheduling.particleLod.proxyUpdateRateHz);
  const double minProxyUpdateInterval =
    proxyUpdateRateHz > 0.0f
      ? 1.0 / static_cast<double>(proxyUpdateRateHz)
      : std::numeric_limits<double>::infinity();
  const bool proxyUpdateIntervalElapsed =
    rs.scene.particleLodLastProxyBuildTime < 0.0 ||
    currentTime - rs.scene.particleLodLastProxyBuildTime >=
      minProxyUpdateInterval;

  if (rs.build.particlesDirty) {
    BuildRenderParticles(input,
                         particleVisual,
                         rs.scene.particles,
                         &rs.scene.stressParticles);
    rs.build.particlesDirty = false;
    ++rs.scene.particlesVersion;
    ++rs.scene.stressParticlesVersion;
    result.particlesBuilt = true;
  }

  const bool lodCanBeUsed =
    scheduling.particleLod.mode != ParticleLodMode::Off ||
    (scheduling.autoParticleLodOnSoftwareRenderer && softwareRenderer);
  if (!lodCanBeUsed) {
    if (rs.scene.particleLod.valid ||
        !rs.scene.particleLodProxy.empty()) {
      rs.scene.particleLod = ParticleLodTree{};
      rs.scene.particleLodGpu = ParticleLodGpuTree{};
      rs.scene.particleLodOrderedParticles.clear();
      rs.scene.particleLodProxy.clear();
      rs.scene.particleLodStressProxy.clear();
      ++rs.scene.particleLodVersion;
    }
    rs.scene.particleLodSettings = scheduling.particleLod;
    rs.scene.particleLodProxyRebuildPending = false;
    if (rs.build.velocityInstancesDirty) {
      UpdateVelocityRenderData(input,
                               velocityState.subtraction,
                               rs.scene.velocityInstances);
      rs.build.velocityInstancesDirty = false;
      ++rs.scene.velocityVersion;
      result.velocityBuilt = true;
    }
    return result;
  }

  const bool lodTreeMissing =
    !rs.scene.particleLod.valid && !rs.scene.particles.empty();
  const bool rebuildLodTree =
    result.particlesBuilt || lodTreeBuildSettingsChanged || lodTreeMissing;
  if (rebuildLodTree) {
    BuildParticleLodTree(rs.scene.particles,
                         scheduling.particleLod,
                         rs.scene.particleLod);
    BuildParticleLodGpuTree(rs.scene.particleLod,
                            rs.scene.particleLodGpu);
    BuildParticleLodOrderedParticles(rs.scene.particles,
                                     rs.scene.particleLod,
                                     rs.scene.particleLodOrderedParticles);
  }

  if (lodFocusMoved) {
    rs.scene.particleLodProxyRebuildPending = true;
  }

  const bool forceProxyRebuild = rebuildLodTree || lodProxySettingsChanged;
  const bool scheduledProxyRebuild =
    rs.scene.particleLodProxyRebuildPending &&
    (!scheduling.interactionActive || proxyUpdateIntervalElapsed);
  const bool skipCpuProxyForGpuLod =
    rs.backend && rs.backend->capabilities().particleGpuLod;

  if (skipCpuProxyForGpuLod) {
    rs.scene.particleLodSettings = scheduling.particleLod;
    rs.scene.particleLodProxy.clear();
    rs.scene.particleLodStressProxy.clear();
    rs.scene.particleLodFocus = camera.cameraTarget;
    rs.scene.particleLodLastProxyBuildTime = currentTime;
    rs.scene.particleLodProxyRebuildPending = false;
    if (forceProxyRebuild) {
      ++rs.scene.particleLodVersion;
    }
  } else if (forceProxyRebuild || scheduledProxyRebuild) {
    std::vector<RenderParticle> nextProxy;
    const bool proxyOk =
      BuildParticleLodProxyDrawList(rs.scene.particles,
                                    rs.scene.particleLod,
                                    camera.cameraTarget,
                                    scheduling.particleLod,
                                    nextProxy);
    rs.scene.particleLodSettings = scheduling.particleLod;
    if (proxyOk) {
      rs.scene.particleLodProxy = std::move(nextProxy);
      rs.scene.particleLodStressProxy.clear();
      rs.scene.particleLodStressProxy.reserve(
        rs.scene.particleLodProxy.size() / 32);
      for (const RenderParticle& particle : rs.scene.particleLodProxy) {
        if (particle.flag_stress != 0) {
          rs.scene.particleLodStressProxy.push_back(particle);
        }
      }
      rs.scene.particleLodFocus = camera.cameraTarget;
      rs.scene.particleLodLastProxyBuildTime = currentTime;
      rs.scene.particleLodProxyRebuildPending = false;
      ++rs.scene.particleLodVersion;
    } else if (forceProxyRebuild) {
      rs.scene.particleLodProxy.clear();
      rs.scene.particleLodStressProxy.clear();
      rs.scene.particleLodLastProxyBuildTime = currentTime;
      rs.scene.particleLodProxyRebuildPending = false;
      ++rs.scene.particleLodVersion;
    } else {
      rs.scene.particleLodLastProxyBuildTime = currentTime;
      rs.scene.particleLodProxyRebuildPending = true;
    }
  }

  if (rs.build.velocityInstancesDirty) {
    UpdateVelocityRenderData(input,
                             velocityState.subtraction,
                             rs.scene.velocityInstances);
    rs.build.velocityInstancesDirty = false;
    ++rs.scene.velocityVersion;
    result.velocityBuilt = true;
  }

  return result;
}

void AcknowledgeParticleRenderBuild(SimulationDataset& particles,
                                    const ParticleRenderBuildResult& result)
{
  if (result.particlesBuilt) {
    particles.particlesDirty = false;
  }

  if (result.velocityBuilt) {
    particles.velocityDirty = false;
  }
}

static void UpdateObjectRenderSceneData(const SceneManagers& scene,
					RenderRuntimeState& render,
					RenderSystem& rs)
{
  if (render.cubes.cpuUpdated || rs.build.cubesDirty) {
    BuildCubeRenderData(scene.cube, rs.scene.cubes);
    render.cubes.cpuUpdated = false;
    rs.build.cubesDirty = false;
    ++rs.scene.cubesVersion;
  }

  if (render.ellipsoids.cpuUpdated || rs.build.ellipsoidsDirty) {
    BuildEllipsoidRenderData(scene.ellipsoid,
                             rs.scene.ellipsoids);
    render.ellipsoids.cpuUpdated = false;
    rs.build.ellipsoidsDirty = false;
    ++rs.scene.ellipsoidsVersion;
  }

  if (render.disks.cpuUpdated || rs.build.disksDirty) {
    BuildDiskRenderData(scene.disk,
                        rs.scene.disks);
    render.disks.cpuUpdated = false;
    rs.build.disksDirty = false;
    ++rs.scene.disksVersion;
  }
}

static void UpdateLineRenderSceneData(const SceneManagers& scene,
                                      RenderRuntimeState& render,
                                      RenderSystem& rs)
{
  if (!render.lines.cpuUpdated &&
      !render.cuboidAnnotations.gpuUpdated &&
      !rs.build.linesDirty) {
    return;
  }

  BuildLineRenderData(scene.line,
                      rs.scene.lines);

  if (render.cuboidAnnotations.show) {
    AppendCuboidArrowRenderData(scene.cuboidAnnotation,
                                rs.scene.lines);
  }

  render.lines.cpuUpdated = false;
  rs.build.linesDirty = false;
  ++rs.scene.linesVersion;
}

#ifdef ISO_CONTOUR
static void UpdateIsoContourRenderSceneData(const IsoContourGeometryState& isoContour,
                                            RenderRuntimeState& render,
                                            RenderSystem& rs)
{
  if (!render.isocontour.cpuUpdated) {
    return;
  }

  BuildIsoContourRenderData(isoContour.verts,
                            isoContour.inds,
                            rs.scene.isoContour);
  render.isocontour.cpuUpdated = false;
  ++rs.scene.isoContourVersion;
}
#endif

#ifdef VOLUME_RENDERING
static void UpdateVolumeRenderSceneData(const VolumeRenderingResultState& volume,
                                        RenderRuntimeState& render,
                                        RenderSystem& rs)
{
  if (!render.volume.cpuUpdated && !rs.build.volumeDirty) {
    return;
  }

  rs.scene.volume = volume.tree;
  render.volume.cpuUpdated = false;
  rs.build.volumeDirty = false;
  ++rs.scene.volumeVersion;
}
#endif

#ifdef USE_CONVEX_HULL
static void UpdateConvexHullRenderState(RenderLayerState& polyhedraState,
					const PolyhedronManager& polyhedra,
					RenderSystem& rs)
{
  if (!polyhedraState.gpuUpdated) {
    return;
  }

  BuildPolyhedronRenderData(polyhedra,
                            rs.scene.polyhedra);

  polyhedraState.gpuUpdated = false;
  ++rs.scene.polyhedraVersion;
}
#endif

static void UpdateCuboidAnnotationRenderSceneData(RenderLayerState& annotationState,
						  const CuboidAnnotationManager& annotations,
						  RenderSystem& rs)
{
  if (!annotationState.gpuUpdated && !rs.build.cuboidsDirty) {
    return;
  }

  rs.scene.cuboids.clear();

  AppendCuboidAnnotationRenderData(annotations,
				   rs.scene.cuboids);

  annotationState.gpuUpdated = false;
  rs.build.cuboidsDirty = false;
  ++rs.scene.cuboidsVersion;
}

ParticleRenderBuildResult UpdateRenderSceneData(const ParticleRenderInput& particleInput,
                                                const ParticleVisualConfig& particleVisual,
                                                const CameraContext& camera,
                                                double currentTime,
                                                bool softwareRenderer,
                                                RenderRuntimeState& render,
                                                const AppDerivedState& derived,
                                                RenderSystem& rs)
{
  PropagateDirtyFlags(particleInput, rs);

  ParticleRenderBuildResult buildResult =
    UpdateParticleRenderSceneData(particleInput,
                                  particleVisual,
                                  camera,
                                  currentTime,
                                  softwareRenderer,
                                  render.velocity,
                                  render.scheduling,
                                  rs);

  UpdateObjectRenderSceneData(derived.scene,
                              render,
                              rs);

  UpdateLineRenderSceneData(derived.scene,
                            render,
                            rs);

#ifdef ISO_CONTOUR
  UpdateIsoContourRenderSceneData(derived.analysis.isoContour,
                                  render,
                                  rs);
#endif

#ifdef VOLUME_RENDERING
  UpdateVolumeRenderSceneData(derived.analysis.volume,
                              render,
                              rs);
#endif

#ifdef USE_CONVEX_HULL
  UpdateConvexHullRenderState(render.polyhedra,
                              derived.scene.polyhedron,
                              rs);
#endif

  UpdateCuboidAnnotationRenderSceneData(render.cuboidAnnotations,
                                        derived.scene.cuboidAnnotation,
                                        rs);

  return buildResult;
}

void UpdateProjectionPreviewTexture(ProjectionPreviewDerivedState& source,
                                    RenderSystem& render)
{
  if (!source.image.valid()) {
    return;
  }

  if (!source.computed) {
    return;
  }

  if (render.backend) {
    render.backend->updateProjectionPreview(source.image);
  }
  source.computed = false;
}
