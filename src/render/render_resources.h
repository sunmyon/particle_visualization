#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "object.h"
#include "render_types.h"

struct ParticleVisualConfig;

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

struct RenderResources {
#ifdef VOLUME_RENDERING
  GLuint fullscreenVAO = 0;
#endif
  
  std::vector<RenderParticle> renderParticles;
  bool particleRenderDataDirty = true;
  bool particlesGpuDirty = true;

  std::vector<float> velocityInstanceData;
  bool velocityInstanceDataDirty = true;
  bool velocityGpuDirty = true;
  
#ifdef ISO_CONTOUR
  IsoContourRenderData isoContourRenderData;
  bool isoContourRenderDataDirty = true;
  bool isoContourGpuDirty = true;
#endif

  std::vector<InstancedSolidItem> cubeRenderData;
  bool cubeRenderDataDirty = true;
  bool cubesGpuDirty = true;
  
  std::vector<InstancedSolidItem> ellipsoidRenderData;
  bool ellipsoidRenderDataDirty = true;
  bool ellipsoidsGpuDirty = true;

  std::vector<InstancedSolidItem> diskRenderData;
  bool diskRenderDataDirty = true;
  bool disksGpuDirty = true;

  std::vector<CuboidRenderItem> cuboidRenderData;
  bool cuboidRenderDataDirty = true;
  bool cuboidsGpuDirty = true;
  
  std::vector<LineRenderItem> lineRenderData;
  bool lineRenderDataDirty = true;
  bool linesGpuDirty = true;

  std::vector<PolyhedronRenderItem> polyhedronRenderData;
  bool polyhedraRenderDataDirty = true;
  bool polyhedraGpuDirty = true;
};

class ParticleData;
class ParticleArray;
void BuildRenderParticles(ParticleArray& P,
                          const ParticleVisualConfig& visualConfig,
                          std::vector<RenderParticle>& out);

std::vector<float> BuildVelocityInstanceData(const TrackingVector<ParticleData>& particles,
                                             const int velocity_subtraction);

void UpdateVelocityRenderData(ParticleArray& particleArray,
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

struct RenderSystem;
void InitRenderSystem(RenderSystem& rs);
void DestroyRenderSystem(RenderSystem& rs);
