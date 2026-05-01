#include <vector>

#include "data/simulation_block.h"
#include "data/sample_coordinates.h"

#include "core/quantity.h"
#include "render/particle_visual_config.h"
#include "render_resources.h"
#include "render/scene_objects.h"

void BuildRenderParticles(const ParticleRenderInput& input,
                          const ParticleVisualConfig& visualConfig,
                          std::vector<RenderParticle>& out,
                          std::vector<RenderParticle>* stressOut)
{
  out.clear();
  if (stressOut) {
    stressOut->clear();
  }

  if (!input.valid()) {
    return;
  }

  const SimulationBlock& block = *input.block;
  out.reserve(block.particles.size());
  if (stressOut) {
    stressOut->reserve(block.particles.size() / 32);
  }

  for (size_t i = 0; i < block.particles.size(); ++i) {
    const auto& p = block.particles[i];

    const int type = p.type;
    if (type < 0 || type >= kNumTypes) {
      continue;
    }
    if (input.visibilityMask &&
        i < input.visibilityMask->size() &&
        (*input.visibilityMask)[i] != 0) {
      continue;
    }
    if (visualConfig.types[type].hideParticles) continue;

    RenderParticle rp;
    renderPosition(p, block.worldToRenderScale, rp.pos);
    rp.type = static_cast<uint8_t>(p.type);
    rp.flag_stress =
      (input.stressFlags && i < input.stressFlags->size())
        ? static_cast<uint8_t>((*input.stressFlags)[i])
        : 0;
    rp.hsml = renderSupportRadius(p, block.worldToRenderScale);
    rp.val_show = getScalarValue(block,
                                 p,
                                 i,
                                 visualConfig.types[type].selectedQuantity);

    out.push_back(rp);
    if (stressOut && rp.flag_stress != 0) {
      stressOut->push_back(rp);
    }
  }
}

std::vector<float> BuildVelocityInstanceData(const std::vector<SimulationElement>& particles,
                                             float worldToRenderScale,
					     const int velocity_subtraction)
{
  std::vector<float> instanceData;
  instanceData.reserve(particles.size() * 6);

  const int stride = (velocity_subtraction > 0)
                   ? velocity_subtraction : 1;

  for (size_t i = 0; i < particles.size(); ++i) {
    if (i % stride != 0) continue;

    const auto& p = particles[i];
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);

    instanceData.push_back(pos.x);
    instanceData.push_back(pos.y);
    instanceData.push_back(pos.z);

    instanceData.push_back(p.vel[0]);
    instanceData.push_back(p.vel[1]);
    instanceData.push_back(p.vel[2]);
  }

  return instanceData;
}


void UpdateVelocityRenderData(const ParticleRenderInput& input,
			      const int velocity_subtraction,
			      std::vector<float>& velocityInstanceData)
{
  if (!input.valid()) {
    velocityInstanceData.clear();
    return;
  }

  velocityInstanceData = BuildVelocityInstanceData(input.block->particles,
                                                   input.block->worldToRenderScale,
                                                   velocity_subtraction);
}


void BuildCubeRenderData(const CubeManager& manager,
                         std::vector<InstancedSolidItem>& out)
{
  const auto& cubes = manager.getCubes();

  out.clear();
  out.reserve(cubes.size());

  for (const auto& c : cubes) {
    InstancedSolidItem item;
    item.model = glm::translate(glm::mat4(1.0f), c.center);
    item.model = glm::scale(item.model, glm::vec3(c.halfSize));
    item.color = glm::vec3(1.0f);
    item.opacity = c.opacity;

    out.push_back(item);
  }
}

void BuildEllipsoidRenderData(const EllipsoidManager& manager,
                              std::vector<InstancedSolidItem>& out)
{
  const auto& ellipsoids = manager.getEllipsoids();

  out.clear();
  out.reserve(ellipsoids.size());

  for (const auto& e : ellipsoids) {
    if (e.renderMode != EllipsoidRenderMode::Solid)
      continue;

    InstancedSolidItem item;
    item.model = e.modelMatrix();
    item.color = e.color;
    item.opacity = e.opacity;
    out.push_back(item);
  }
}

void BuildDiskRenderData(const DiskManager& manager,
                         std::vector<InstancedSolidItem>& out)
{
  const auto& disks = manager.getDisks();

  out.clear();
  out.reserve(disks.size());

  for (const auto& d : disks) {
    InstancedSolidItem item;
    item.model = d.modelMatrix();
    item.color = d.color;
    item.opacity = d.opacity;
    out.push_back(item);
  }
}

void BuildPolyhedronRenderData(const PolyhedronManager& manager,
                               std::vector<PolyhedronRenderItem>& out)
{
  out.clear();

  const auto& objects = manager.getObjects();
  out.reserve(objects.size());

  for (const auto& [id, obj] : objects) {
    if (obj.vertices.empty())
      continue;

    PolyhedronRenderItem item;
    item.id = id;
    item.vertices = obj.vertices;
    item.color = obj.color;
    item.opacity = obj.opacity;
    item.tag = obj.tag;

    out.push_back(std::move(item));
  }
}

void AppendCuboidAnnotationRenderData(const CuboidAnnotationManager& manager,
                                      std::vector<CuboidRenderItem>& out)
{
  const auto& objects = manager.objects();
  out.reserve(out.size() + objects.size());

  for (const auto& obj : objects) {
    CuboidRenderItem item;
    item.cuboid = obj.cuboid;
    item.highlightColor = obj.highlightColor;
    item.showHighlight = obj.showAxisHighlight;
    item.selectedAxis = obj.selectedAxis;
    out.push_back(std::move(item));
  }
}

ArrowObject buildArrowFromCuboidAnnotation(const CuboidAnnotationObject& obj)
{
  ArrowObject arrow;
  arrow.color      = obj.arrowColor;
  arrow.length     = obj.arrowLength;
  arrow.headLength = obj.arrowHeadLength;
  arrow.headWidth  = obj.arrowHeadWidth;
  arrow.tag        = obj.tag;

  const glm::mat3 R = glm::mat3_cast(obj.cuboid.orientation);

  glm::vec3 axisLocal(0.0f);
  float extent = obj.cuboid.halfSize.z;

  if (obj.selectedAxis == CuboidAxis::X) {
    axisLocal = glm::vec3(1.0f, 0.0f, 0.0f);
    extent = obj.cuboid.halfSize.x;
  } else if (obj.selectedAxis == CuboidAxis::Y) {
    axisLocal = glm::vec3(0.0f, 1.0f, 0.0f);
    extent = obj.cuboid.halfSize.y;
  } else {
    axisLocal = glm::vec3(0.0f, 0.0f, 1.0f);
    extent = obj.cuboid.halfSize.z;
  }

  glm::vec3 axisWorld = glm::normalize(R * axisLocal);
  glm::vec3 faceCenter = obj.cuboid.center + axisWorld * extent;

  arrow.origin = faceCenter;
  arrow.direction = axisWorld;
  return arrow;
}


void AppendCuboidArrowRenderData(const CuboidAnnotationManager& manager,
				 std::vector<LineRenderItem>& out)
{
  const auto& objects = manager.objects();
  out.reserve(out.size() + objects.size());

  for (const auto& obj : objects) {
    if (!obj.showArrow) continue;

    ArrowObject arrow = buildArrowFromCuboidAnnotation(obj);
    arrow.color = obj.arrowColor;

    LineRenderItem item;
    item.color = glm::vec3(arrow.color);
    item.opacity = arrow.color.a;
    item.mode = LinePrimitiveMode::List;

    const glm::vec3 dir = glm::normalize(arrow.direction);
    const glm::vec3 tip = arrow.origin + dir * arrow.length;
    const glm::vec3 base = tip - dir * arrow.headLength;

    glm::vec3 arbitrary(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(dir, arbitrary)) > 0.9f)
      arbitrary = glm::vec3(1.0f, 0.0f, 0.0f);

    const glm::vec3 u = glm::normalize(glm::cross(dir, arbitrary));
    const glm::vec3 side1 = base + u * arrow.headWidth;
    const glm::vec3 side2 = base - u * arrow.headWidth;

    item.points.reserve(6);
    item.points.push_back(arrow.origin);
    item.points.push_back(tip);
    item.points.push_back(tip);
    item.points.push_back(side1);
    item.points.push_back(tip);
    item.points.push_back(side2);

    out.push_back(std::move(item));
  }
}

void BuildLineRenderData(const LineManager& manager,
                         std::vector<LineRenderItem>& out)
{
  const auto& lines = manager.getLines();

  out.clear();
  out.reserve(lines.size());

  for (const auto& line : lines) {
    if (line.empty()) continue;
    if (line.points.empty()) continue;

    LineRenderItem item;
    item.points.assign(line.points.begin(), line.points.end());
    item.color = line.color;
    item.opacity = line.opacity;

    out.push_back(std::move(item));
  }
}


#ifdef ISO_CONTOUR
namespace {
  void computeIsoContourNormals(const std::vector<float>& verts,
				const std::vector<unsigned>& inds,
				std::vector<float>& out_normals)
  {
    const size_t vcount = verts.size() / 3;
    std::vector<glm::vec3> normals(vcount, glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < inds.size(); i += 3) {
      unsigned i0 = inds[i + 0];
      unsigned i1 = inds[i + 1];
      unsigned i2 = inds[i + 2];

      glm::vec3 v0(verts[3 * i0 + 0], verts[3 * i0 + 1], verts[3 * i0 + 2]);
      glm::vec3 v1(verts[3 * i1 + 0], verts[3 * i1 + 1], verts[3 * i1 + 2]);
      glm::vec3 v2(verts[3 * i2 + 0], verts[3 * i2 + 1], verts[3 * i2 + 2]);

      glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
      if (glm::length(n) > 1.0e-6f)
	n = glm::normalize(n);

      normals[i0] += n;
      normals[i1] += n;
      normals[i2] += n;
    }

    out_normals.resize(verts.size());
    for (size_t v = 0; v < vcount; ++v) {
      glm::vec3 n = normals[v];
      if (glm::length(n) > 1.0e-6f)
	n = glm::normalize(n);
      else
	n = glm::vec3(0.0f, 0.0f, 1.0f);

      out_normals[3 * v + 0] = n.x;
      out_normals[3 * v + 1] = n.y;
      out_normals[3 * v + 2] = n.z;
    }
  }
}

void BuildIsoContourRenderData(const std::vector<float>& verts,
                               const std::vector<unsigned>& inds,
                               IsoContourRenderData& out)
{
  out.verts = verts;
  out.inds = inds;

  if (verts.empty() || inds.empty()) {
    out.normals.clear();
    return;
  }

  computeIsoContourNormals(out.verts, out.inds, out.normals);
}
#endif
