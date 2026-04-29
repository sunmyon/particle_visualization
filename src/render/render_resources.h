#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "core/tracking_vector.h"
#include "render/scene_objects.h"
#include "render_types.h"

#ifdef VOLUME_RENDERING
#include "volume/adaptive_volume_tree.h"
#endif

struct ParticleVisualConfig;
struct ParticleBlock;

struct RenderParticle {
  float pos[3];
  std::uint8_t type;
  std::uint8_t flag_stress;
  std::uint16_t pad = 0;
  float hsml;
  float val_show;
};

using EllipsoidRenderItem = InstancedSolidItem;
using DiskRenderItem = InstancedSolidItem;
using CubeRenderItem = InstancedSolidItem;

enum class LinePrimitiveMode {
  Strip,
  List
};

struct LineRenderItem {
  std::vector<glm::vec3> points;
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
  LinePrimitiveMode mode = LinePrimitiveMode::Strip;
};

struct CuboidRenderItem {
  CuboidObject cuboid;
  glm::vec4 highlightColor{1.0f};
  bool showHighlight = false;
  CuboidAxis selectedAxis = CuboidAxis::Z;
};

struct PolyhedronRenderItem {
  std::vector<glm::vec3> vertices;
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
  std::string tag;
  int id = -1;
};

#ifdef ISO_CONTOUR
struct IsoContourRenderData {
  TrackingVector<float> verts;
  TrackingVector<float> normals;
  TrackingVector<unsigned> inds;
};
#endif

using RenderSceneVersion = std::uint64_t;

struct RenderSceneData {
  std::vector<RenderParticle> particles;
  RenderSceneVersion particlesVersion = 1;

  std::vector<float> velocityInstances;
  RenderSceneVersion velocityVersion = 1;
  
#ifdef ISO_CONTOUR
  IsoContourRenderData isoContour;
  RenderSceneVersion isoContourVersion = 1;
#endif

#ifdef VOLUME_RENDERING
  AdaptiveVolumeTree volume;
  RenderSceneVersion volumeVersion = 1;
#endif

  std::vector<CubeRenderItem> cubes;
  RenderSceneVersion cubesVersion = 1;
  
  std::vector<EllipsoidRenderItem> ellipsoids;
  RenderSceneVersion ellipsoidsVersion = 1;

  std::vector<DiskRenderItem> disks;
  RenderSceneVersion disksVersion = 1;

  std::vector<CuboidRenderItem> cuboids;
  RenderSceneVersion cuboidsVersion = 1;
  
  std::vector<LineRenderItem> lines;
  RenderSceneVersion linesVersion = 1;

  std::vector<PolyhedronRenderItem> polyhedra;
  RenderSceneVersion polyhedraVersion = 1;
};

struct RenderSceneBuildState {
  bool particlesDirty = true;
  bool velocityInstancesDirty = true;

#ifdef ISO_CONTOUR
  bool isoContourDirty = true;
#endif

#ifdef VOLUME_RENDERING
  bool volumeDirty = true;
#endif

  bool cubesDirty = true;
  bool ellipsoidsDirty = true;
  bool disksDirty = true;
  bool cuboidsDirty = true;
  bool linesDirty = true;
  bool polyhedraDirty = true;
};

class ParticleData;

struct ParticleRenderInput {
  const ParticleBlock* block = nullptr;
  const TrackingVector<uint8_t>* visibilityMask = nullptr;
  bool particlesDirty = false;
  bool velocityDirty = false;

  bool valid() const { return block != nullptr; }
};

void BuildRenderParticles(const ParticleRenderInput& input,
                          const ParticleVisualConfig& visualConfig,
                          std::vector<RenderParticle>& out);

std::vector<float> BuildVelocityInstanceData(const TrackingVector<ParticleData>& particles,
                                             const int velocity_subtraction);

void UpdateVelocityRenderData(const ParticleRenderInput& input,
			      const int velocity_subtraction,
			      std::vector<float>& velocityInstanceData);

void BuildCubeRenderData(const CubeManager& manager,
                         std::vector<CubeRenderItem>& out);

void BuildDiskRenderData(const DiskManager& manager,
                         std::vector<DiskRenderItem>& out);

void BuildEllipsoidRenderData(const EllipsoidManager& manager,
                              std::vector<EllipsoidRenderItem>& out);

void BuildLineRenderData(const LineManager& manager,
                         std::vector<LineRenderItem>& out);

#ifdef ISO_CONTOUR
void BuildIsoContourRenderData(const TrackingVector<float>& verts,
                               const TrackingVector<unsigned>& inds,
                               IsoContourRenderData& out);
#endif

void BuildPolyhedronRenderData(const PolyhedronManager& manager,
                               std::vector<PolyhedronRenderItem>& out);

void AppendCuboidAnnotationRenderData(const CuboidAnnotationManager& manager,
                                      std::vector<CuboidRenderItem>& out);

void AppendCuboidArrowRenderData(const CuboidAnnotationManager& manager,
                                     std::vector<LineRenderItem>& out);

ArrowObject BuildArrowFromCuboidAnnotation(const CuboidAnnotationObject& obj);
