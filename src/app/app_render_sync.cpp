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
    rs.resources.particleRenderDataDirty = true;
  }

  if (input.velocityDirty) {
    rs.resources.velocityInstanceDataDirty = true;
  }
}

static ParticleRenderUploadResult UpdateParticleRenderResources(const ParticleRenderInput& input,
                                                                const ParticleVisualConfig& particleVisual,
                                                                const VelocityRenderState& velocityState,
                                                                RenderSystem& rs)
{
  ParticleRenderUploadResult result;

  if (rs.resources.particleRenderDataDirty) {
    BuildRenderParticles(input,
                         particleVisual,
                         rs.resources.renderParticles);
    rs.resources.particleRenderDataDirty = false;
    rs.resources.particlesGpuDirty = true;
    result.particlesUploaded = true;
  }

  if (rs.resources.velocityInstanceDataDirty) {
    UpdateVelocityRenderData(input,
                             velocityState.subtraction,
                             rs.resources.velocityInstanceData);
    rs.resources.velocityInstanceDataDirty = false;
    rs.resources.velocityGpuDirty = true;
    result.velocityUploaded = true;
  }

  return result;
}

void AcknowledgeParticleRenderUploads(ParticleArray& particles,
                                      const ParticleRenderUploadResult& result)
{
  if (result.particlesUploaded) {
    particles.particlesDirty = false;
  }

  if (result.velocityUploaded) {
    particles.velocityDirty = false;
  }
}

static void UpdateSceneRenderResources(const SceneManagers& scene,
				       RenderRuntimeState& render,
				       RenderSystem& rs)
{
  if (render.cubes.cpuUpdated || rs.resources.cubeRenderDataDirty) {
    BuildCubeRenderData(scene.cube, rs.resources.cubeRenderData);
    render.cubes.cpuUpdated = false;
    rs.resources.cubeRenderDataDirty = false;
    rs.resources.cubesGpuDirty = true;
  }

  if (render.ellipsoids.cpuUpdated || rs.resources.ellipsoidRenderDataDirty) {
    BuildEllipsoidRenderData(scene.ellipsoid,
                             rs.resources.ellipsoidRenderData);
    render.ellipsoids.cpuUpdated = false;
    rs.resources.ellipsoidRenderDataDirty = false;
    rs.resources.ellipsoidsGpuDirty = true;
  }

  if (render.disks.cpuUpdated || rs.resources.diskRenderDataDirty) {
    BuildDiskRenderData(scene.disk,
                        rs.resources.diskRenderData);
    render.disks.cpuUpdated = false;
    rs.resources.diskRenderDataDirty = false;
    rs.resources.disksGpuDirty = true;
  }
}

static void UpdateLineRenderResources(const SceneManagers& scene,
                                      RenderRuntimeState& render,
                                      RenderSystem& rs)
{
  if (!render.lines.cpuUpdated &&
      !render.cuboidAnnotations.gpuUpdated &&
      !rs.resources.lineRenderDataDirty) {
    return;
  }

  BuildLineRenderData(scene.line,
                      rs.resources.lineRenderData);

  if (render.cuboidAnnotations.show) {
    AppendCuboidArrowRenderData(scene.cuboidAnnotation,
                                rs.resources.lineRenderData);
  }

  render.lines.cpuUpdated = false;
  rs.resources.lineRenderDataDirty = false;
  rs.resources.linesGpuDirty = true;
}

#ifdef ISO_CONTOUR
static void UpdateIsoContourRenderResources(const IsoContourGeometryState& isoContour,
                                            RenderRuntimeState& render,
                                            RenderSystem& rs)
{
  if (!render.isocontour.cpuUpdated) {
    return;
  }

  BuildIsoContourRenderData(isoContour.verts,
                            isoContour.inds,
                            rs.resources.isoContourRenderData);
  render.isocontour.cpuUpdated = false;
  rs.resources.isoContourGpuDirty = true;
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
                            rs.resources.polyhedronRenderData);

  polyhedraState.gpuUpdated = false;
  rs.resources.polyhedraGpuDirty = true;
  rs.polyhedron.requestResetGroup("convex_hull");
}
#endif

static void UpdateCuboidAnnotationRenderResources(RenderLayerState& annotationState,
						  const CuboidAnnotationManager& annotations,
						  RenderSystem& rs)
{
  if (!annotationState.gpuUpdated && !rs.resources.cuboidRenderDataDirty) {
    return;
  }

  rs.resources.cuboidRenderData.clear();

  AppendCuboidAnnotationRenderData(annotations,
				   rs.resources.cuboidRenderData);
  rs.resources.cuboidsGpuDirty = true;

  annotationState.gpuUpdated = false;
  rs.resources.cuboidRenderDataDirty = false;
}

ParticleRenderUploadResult UpdateRenderResources(const ParticleRenderInput& particleInput,
                                                 const ParticleVisualConfig& particleVisual,
                                                 RenderRuntimeState& render,
                                                 const AppDerivedState& derived,
                                                 RenderSystem& rs)
{
  PropagateDirtyFlags(particleInput, rs);

  ParticleRenderUploadResult uploadResult =
    UpdateParticleRenderResources(particleInput,
                                  particleVisual,
                                  render.velocity,
                                  rs);

  UpdateSceneRenderResources(derived.scene,
                             render,
                             rs);

  UpdateLineRenderResources(derived.scene,
                            render,
                            rs);

#ifdef ISO_CONTOUR
  UpdateIsoContourRenderResources(derived.analysis.isoContour,
                                  render,
                                  rs);
#endif

#ifdef USE_CONVEX_HULL
  UpdateConvexHullRenderState(render.polyhedra,
                              derived.scene.polyhedron,
                              rs);
#endif

  UpdateCuboidAnnotationRenderResources(render.cuboidAnnotations,
                                        derived.scene.cuboidAnnotation,
                                        rs);

  return uploadResult;
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

  render.preview.update(source.image);
  source.computed = false;
}
