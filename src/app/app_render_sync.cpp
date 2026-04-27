#include "app/app_render_sync.h"

#include "app/state/app_state.h"
#include "app/state/render_runtime_state.h"
#include "data/particle_array.h"
#include "particle_visual_config.h"
#include "render/render_resources.h"
#include "render/render_system.h"

ParticleRenderInput MakeParticleRenderInput(const ParticleArray& particles)
{
  return ParticleRenderInput{
    &particles.particleBlock,
    &particles.flag_mask,
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
                                                               const VelocityRenderState& velocityState,
                                                               RenderSystem& rs)
{
  ParticleRenderBuildResult result;

  if (rs.build.particlesDirty) {
    BuildRenderParticles(input,
                         particleVisual,
                         rs.scene.particles);
    rs.build.particlesDirty = false;
    ++rs.scene.particlesVersion;
    result.particlesBuilt = true;
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

void AcknowledgeParticleRenderBuild(ParticleArray& particles,
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
                                                RenderRuntimeState& render,
                                                const AppDerivedState& derived,
                                                RenderSystem& rs)
{
  PropagateDirtyFlags(particleInput, rs);

  ParticleRenderBuildResult buildResult =
    UpdateParticleRenderSceneData(particleInput,
                                  particleVisual,
                                  render.velocity,
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
