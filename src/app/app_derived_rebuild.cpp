#include "app/app_derived_rebuild.h"

#include "app/state/app_state.h"
#include "app/state/render_runtime_state.h"
#include "data/simulation_dataset.h"
#include "interaction/camera.h"
#include "projection/projection_map_tool_state.h"

#include <algorithm>
#include <cmath>

static bool UpdateOverlayState(const SimulationDataset& particles,
                               const CameraContext& camera,
                               const ParticleLabelRenderState& state,
                               OverlayState& overlay)
{
  auto& labels = overlay.particleLabels;

  if (!state.show) {
    labels.clear();
    return false;
  }

  if (labels.needsRebuild(particles, camera, state)) {
    labels.rebuild(particles, camera, state);
    return true;
  }

  return false;
}

static void ApplyRenderScale(CubeObject& cube, float scale)
{
  cube.center *= scale;
  cube.halfSize *= scale;
}

static void ApplyRenderScale(EllipsoidObject& ellipsoid, float scale)
{
  ellipsoid.position *= scale;
  ellipsoid.radii *= scale;
}

static void ApplyRenderScale(DiskObject& disk, float scale)
{
  disk.position *= scale;
  disk.radius *= scale;
}

static void ApplyRenderScale(LineObject& line, float scale)
{
  for (glm::vec3& point : line.points) {
    point *= scale;
  }
}

static void ApplyRenderScale(PolyhedronObject& polyhedron, float scale)
{
  for (glm::vec3& vertex : polyhedron.vertices) {
    vertex *= scale;
  }
}

static void ApplyRenderScale(CuboidObject& cuboid, float scale)
{
  cuboid.center *= scale;
  cuboid.halfSize *= scale;
}

static void ApplyRenderScale(CuboidAnnotationObject& annotation, float scale)
{
  ApplyRenderScale(annotation.cuboid, scale);
  annotation.arrowLength *= scale;
  annotation.arrowHeadLength *= scale;
  annotation.arrowHeadWidth *= scale;
}

#ifdef GEOMETRICAL_ANALYSIS
static DerivedLayerUpdate RebuildDiskDerivedState(DiskAnalysisResultState& result,
                                                  float worldToRenderScale,
                                                  float opacity,
                                                  DiskManager& disks)
{
  if (!result.cpuUpdated) {
    return {};
  }

  disks.clearGroup("main_disk");

  bool visible = false;
  if (result.valid) {
    DiskObject disk = result.disk;
    ApplyRenderScale(disk, worldToRenderScale);
    disk.opacity = opacity;
    disk.color   = glm::vec3(1.0f);
    disk.tag     = "main_disk";
    disks.add(disk);
    visible = true;
  }

  return {true, visible};
}

static DerivedLayerUpdate RebuildEllipsoidDerivedState(EllipsoidAnalysisResultState& result,
                                                       float worldToRenderScale,
                                                       float opacity,
                                                       EllipsoidManager& ellipsoids)
{
  if (!result.cpuUpdated) {
    return {};
  }

  ellipsoids.clearGroup("analysis_ellipsoid");

  bool visible = false;
  if (result.valid) {
    EllipsoidObject obj = result.ellipsoid;
    ApplyRenderScale(obj, worldToRenderScale);
    obj.opacity = opacity;
    obj.color   = glm::vec3(1.0f);
    obj.tag     = "analysis_ellipsoid";
    obj.renderMode = EllipsoidRenderMode::Solid;
    ellipsoids.add(obj);
    visible = true;
  }

  return {true, visible};
}
#endif

#ifdef STREAM_LINE
static DerivedLayerUpdate RebuildStreamlinePreviewDerivedState(StreamlinePreviewResultState& result,
                                                               float worldToRenderScale,
                                                               CubeManager& cubes)
{
  if (!result.cpuUpdated) {
    return {};
  }

  cubes.clearGroup("streamline_seed_region");

  bool visible = false;
  if (result.valid) {
    CubeObject cube = result.cube;
    ApplyRenderScale(cube, worldToRenderScale);
    cube.tag = "streamline_seed_region";
    cubes.add(cube);
    visible = true;
  }

  return {true, visible};
}

static DerivedLayerUpdate RebuildStreamlineDerivedState(StreamlineBuildResultState& result,
                                                        float worldToRenderScale,
                                                        LineManager& lines)
{
  if (!result.cpuUpdated) {
    return {};
  }

  lines.clearGroup("streamline");

  for (const auto& source : result.lines) {
    LineObject line = source;
    ApplyRenderScale(line, worldToRenderScale);
    lines.add(line);
  }

  const bool visible = !result.lines.empty();
  return {true, visible};
}
#endif

#ifdef POWER_SPECTRUM
static DerivedLayerUpdate RebuildPowerSpectrumRegionDerivedState(PowerSpectrumResultState& result,
                                                                 float worldToRenderScale,
                                                                 CubeManager& cubes)
{
  if (!result.regionPreviewCpuUpdated) {
    return {};
  }

  cubes.clearGroup("power_spectrum_region");

  bool visible = false;
  if (result.regionPreviewValid) {
    CubeObject cube = result.regionCube;
    ApplyRenderScale(cube, worldToRenderScale);
    cube.tag = "power_spectrum_region";
    cubes.add(cube);
    visible = true;
  }

  return {true, visible};
}
#endif

static bool RebuildSnapshotExtractRegionDerivedState(SnapshotExtractPreviewState& result,
                                                     float worldToRenderScale,
                                                     CubeManager& cubes,
                                                     EllipsoidManager& ellipsoids)
{
  if (!result.cpuUpdated) {
    return false;
  }

  cubes.clearGroup("snapshot_extract_region");
  ellipsoids.clearGroup("snapshot_extract_region");

  if (result.valid) {
    if (result.regionKind == SnapshotExtractRegionKind::Sphere) {
      EllipsoidObject sphere = result.sphere;
      ApplyRenderScale(sphere, worldToRenderScale);
      sphere.tag = "snapshot_extract_region";
      ellipsoids.add(sphere);
    } else {
      CubeObject box = result.box;
      ApplyRenderScale(box, worldToRenderScale);
      box.tag = "snapshot_extract_region";
      cubes.add(box);
    }
  }

  return true;
}

#ifdef USE_CONVEX_HULL
static DerivedLayerUpdate RebuildConvexHullDerivedState(ConvexHullRuntimeState& convexState,
                                                        bool requested,
                                                        float worldToRenderScale,
                                                        float opacity,
                                                        PolyhedronManager& polyhedra)
{
  const bool scaleChanged =
    std::abs(convexState.renderedWorldToRenderScale - worldToRenderScale) >
    std::max(1.0e-6f, 1.0e-6f * std::abs(worldToRenderScale));

  if (!requested && !scaleChanged) {
    return {};
  }

  polyhedra.clearGroup("convex_hull");
  convexState.renderedWorldToRenderScale = worldToRenderScale;

  bool anyVisible = false;
  for (const auto& entry : convexState.entries) {
    if (!entry.visible || entry.lineVertices.empty()) {
      continue;
    }

    PolyhedronObject obj;
    obj.vertices.reserve(entry.lineVertices.size() / 3);
    for (size_t k = 0; k + 2 < entry.lineVertices.size(); k += 3) {
      obj.vertices.emplace_back(entry.lineVertices[k],
                                entry.lineVertices[k + 1],
                                entry.lineVertices[k + 2]);
    }
    ApplyRenderScale(obj, worldToRenderScale);

    obj.color   = glm::vec3(1.0f);
    obj.opacity = opacity;
    obj.tag     = "convex_hull";

    polyhedra.add(entry.sourceId, obj);
    anyVisible = true;
  }

  return {true, anyVisible};
}
#endif

static DerivedLayerUpdate UpdateCuboidAnnotationDerivedState(bool requested,
                                               bool showAnnotation,
                                               CuboidAnnotationManager& annotations,
                                               const ProjectionMapToolState& rt,
                                               float worldToRenderScale)
{
  if (!requested) {
    return {};
  }

  annotations.clear();

  if (showAnnotation) {
    CuboidAnnotationObject obj;
    obj.cuboid = rt.interactiveCuboid;

    obj.cuboid.edgeColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    obj.highlightColor   = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    obj.arrowColor       = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    const int axis = rt.params.selectedAxis;
    if (axis == 0)      obj.selectedAxis = CuboidAxis::X;
    else if (axis == 1) obj.selectedAxis = CuboidAxis::Y;
    else                obj.selectedAxis = CuboidAxis::Z;

    obj.showAxisHighlight = true;
    obj.showArrow         = true;
    obj.arrowLength       = 0.2f;
    obj.arrowHeadLength   = 0.05f;
    obj.arrowHeadWidth    = 0.03f;
    obj.tag               = "interactive_cuboid";
    ApplyRenderScale(obj, worldToRenderScale);

    annotations.add(obj);
  }

  return {true, showAnnotation};
}

DerivedRebuildResult RebuildDerivedState(const SimulationDataset& particles,
                                         const CameraContext& camera,
                                         AppDerivedState& derived,
                                         const RenderRuntimeState& render,
                                         const ProjectionMapToolState& projection)
{
  DerivedRebuildResult rebuild;

#ifdef GEOMETRICAL_ANALYSIS
  rebuild.disk =
    RebuildDiskDerivedState(derived.analysis.disk,
                            particles.simulationBlock.worldToRenderScale,
                            render.disks.opacity,
                            derived.scene.disk);

  rebuild.ellipsoid =
    RebuildEllipsoidDerivedState(derived.analysis.ellipsoid,
                                 particles.simulationBlock.worldToRenderScale,
                                 render.ellipsoids.opacity,
                                 derived.scene.ellipsoid);
#endif

#ifdef STREAM_LINE
  rebuild.cube =
    RebuildStreamlinePreviewDerivedState(derived.analysis.streamlinePreview,
                                         particles.simulationBlock.worldToRenderScale,
                                         derived.scene.cube);

  rebuild.line =
    RebuildStreamlineDerivedState(derived.analysis.streamlineBuild,
                                  particles.simulationBlock.worldToRenderScale,
                                  derived.scene.line);
#endif

#ifdef POWER_SPECTRUM
  {
    const DerivedLayerUpdate powerRegion =
      RebuildPowerSpectrumRegionDerivedState(derived.analysis.powerSpectrum,
                                             particles.simulationBlock.worldToRenderScale,
                                             derived.scene.cube);
    if (powerRegion.changed) {
      rebuild.cube.changed = true;
    }
  }
#endif

  if (RebuildSnapshotExtractRegionDerivedState(
        derived.analysis.snapshotExtractPreview,
        particles.simulationBlock.worldToRenderScale,
        derived.scene.cube,
        derived.scene.ellipsoid)) {
    rebuild.cube.changed = true;
    rebuild.ellipsoid.changed = true;
  }

  if (rebuild.cube.changed) {
    rebuild.cube.visible = derived.scene.cube.show();
  }
  if (rebuild.ellipsoid.changed) {
    rebuild.ellipsoid.visible = derived.scene.ellipsoid.show();
  }

  rebuild.particleLabelsUpdated =
    UpdateOverlayState(particles,
                       camera,
                       render.particleLabels,
                       derived.overlay);

#ifdef USE_CONVEX_HULL
  rebuild.polyhedron =
    RebuildConvexHullDerivedState(derived.analysis.convexHulls,
                                  render.polyhedra.cpuUpdated,
                                  particles.simulationBlock.worldToRenderScale,
                                  render.polyhedra.opacity,
                                  derived.scene.polyhedron);
#endif

  rebuild.cuboidAnnotation =
    UpdateCuboidAnnotationDerivedState(render.cuboidAnnotations.cpuUpdated,
                                       render.cuboidAnnotations.show,
                                       derived.scene.cuboidAnnotation,
                                       projection,
                                       particles.simulationBlock.worldToRenderScale);

  if (rebuild.line.changed || rebuild.cuboidAnnotation.changed) {
    rebuild.lineLayer.changed = true;
    rebuild.lineLayer.visible =
      derived.scene.line.show() || derived.scene.cuboidAnnotation.show();
  }

  return rebuild;
}

void AcknowledgeDerivedRebuild(SimulationDataset& particles,
                               AppDerivedState& derived,
                               const DerivedRebuildResult& rebuild)
{
#ifdef GEOMETRICAL_ANALYSIS
  if (rebuild.disk.changed) {
    derived.analysis.disk.cpuUpdated = false;
  }

  if (rebuild.ellipsoid.changed) {
    derived.analysis.ellipsoid.cpuUpdated = false;
  }
#endif

#ifdef STREAM_LINE
  if (rebuild.cube.changed) {
    derived.analysis.streamlinePreview.cpuUpdated = false;
  }

  if (rebuild.line.changed) {
    derived.analysis.streamlineBuild.cpuUpdated = false;
  }
#endif

#ifdef POWER_SPECTRUM
  if (rebuild.cube.changed) {
    derived.analysis.powerSpectrum.regionPreviewCpuUpdated = false;
  }
#endif

  if (rebuild.cube.changed || rebuild.ellipsoid.changed) {
    derived.analysis.snapshotExtractPreview.cpuUpdated = false;
  }

  if (rebuild.particleLabelsUpdated) {
    particles.flagParticleIndexDirty = false;
  }
}

void ApplyDerivedRenderInvalidation(const DerivedRebuildResult& rebuild,
                                    RenderRuntimeState& render)
{
#ifdef GEOMETRICAL_ANALYSIS
  if (rebuild.disk.changed) {
    render.disks.show = rebuild.disk.visible;
    render.disks.cpuUpdated = true;
  }

  if (rebuild.ellipsoid.changed) {
    render.ellipsoids.show = rebuild.ellipsoid.visible;
    render.ellipsoids.cpuUpdated = true;
  }
#endif

  if (rebuild.cube.changed) {
    render.cubes.show = rebuild.cube.visible;
    render.cubes.cpuUpdated = true;
  }

  if (rebuild.ellipsoid.changed) {
    render.ellipsoids.show = rebuild.ellipsoid.visible;
    render.ellipsoids.cpuUpdated = true;
  }

#ifdef STREAM_LINE
  if (rebuild.line.changed) {
    render.lines.cpuUpdated = true;
  }
#endif

#ifdef USE_CONVEX_HULL
  if (rebuild.polyhedron.changed) {
    render.polyhedra.show = rebuild.polyhedron.visible;
    render.polyhedra.cpuUpdated = false;
    render.polyhedra.gpuUpdated = true;
  }
#endif

  if (rebuild.cuboidAnnotation.changed) {
    render.cuboids.show = rebuild.cuboidAnnotation.visible;
    render.cuboidAnnotations.cpuUpdated = false;
    render.cuboidAnnotations.gpuUpdated = true;
  }

  if (rebuild.lineLayer.changed) {
    render.lines.show = rebuild.lineLayer.visible;
  }

}
