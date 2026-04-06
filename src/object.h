#pragma once

#include <unordered_map>
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // for translate, scale
#include <glm/gtc/quaternion.hpp>         // for mat4_cast

#include "core/tracking_vector.h"

void UpdateCuboidTransformArcball(glm::vec3 &center, glm::quat &cuboidTransform
				  , float lastX, float lastY, float xpos, float ypos
				  , const glm::mat4 &view, const glm::vec3 &pivotWorld);

struct Cube {
  glm::vec3 position;       // Center position of the cube
  glm::vec3 size;           // Half-extents along each axis (width/2, height/2, depth/2)
  glm::quat orientation;    // Rotation quaternion
  float opacity;
  std::string tag;          // Group identifier
  
  // Convenience: compute model matrix for this cube
  glm::mat4 modelMatrix() const {
    glm::mat4 M(1.0f);
    M = glm::translate(M, position);
    M = M * glm::mat4_cast(orientation);
    M = glm::scale(M, size * 2.0f);

    return M;
  }
};

class CubeManager {
public:
    CubeManager() = default;
    ~CubeManager() = default;

    CubeManager(const CubeManager&) = delete;
    CubeManager& operator=(const CubeManager&) = delete;

    CubeManager(CubeManager&&) = default;
    CubeManager& operator=(CubeManager&&) = default;

    void addCube(const glm::vec3& position,
                 const glm::vec3& size,
                 const glm::quat& orientation = glm::quat{1,0,0,0},
                 const float& opacity = 0.5f,
                 const std::string& tag = "default") {
      cubes_.push_back({ position, size, orientation, opacity, tag });
    }

    void clearGroup(const std::string& tag) {
      cubes_.erase(
        std::remove_if(cubes_.begin(), cubes_.end(),
                       [&](const Cube& c){ return c.tag == tag; }),
        cubes_.end());
    }

    void removeCube(size_t index) {
      if (index < cubes_.size()) {
        cubes_.erase(cubes_.begin() + index);
      }
    }

    void clear() {
      cubes_.clear();
    }

    const std::vector<Cube>& getCubes() const {
      return cubes_;
    }

    bool show() const { return !cubes_.empty(); }

private:
    std::vector<Cube> cubes_;
};

enum class EllipsoidRenderMode {
  Wireframe,
  Solid
};

struct EllipsoidObject {
  glm::vec3 position{0.0f};
  glm::vec3 radii{1.0f};
  glm::quat orientation{1,0,0,0};
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  EllipsoidRenderMode renderMode = EllipsoidRenderMode::Solid;
  std::string tag;
  
  void clear()  {
    position = glm::vec3(0.0f);
    radii = glm::vec3(0.0f);
    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }

  void set(const glm::vec3& pos,
           const glm::vec3& rad,
           const glm::quat& rot)  {
    position = pos;
    radii = rad;
    orientation = rot;
  }
  
  glm::mat4 modelMatrix() const {
    glm::mat4 M(1.0f);
    M = glm::translate(M, position);
    M = M * glm::mat4_cast(orientation);
    M = glm::scale(M, radii * 2.0f);
    return M;
  }
};

class EllipsoidManager {
public:
  EllipsoidManager() = default;
  ~EllipsoidManager() = default;

  EllipsoidManager(const EllipsoidManager&) = delete;
  EllipsoidManager& operator=(const EllipsoidManager&) = delete;

  EllipsoidManager(EllipsoidManager&&) = default;
  EllipsoidManager& operator=(EllipsoidManager&&) = default;

  void add(const EllipsoidObject& e) {
    ellipsoids_.push_back(e);
  }

  void clearGroup(const std::string& tag) {
    ellipsoids_.erase(
      std::remove_if(ellipsoids_.begin(), ellipsoids_.end(),
                     [&](const EllipsoidObject& e){ return e.tag == tag; }),
      ellipsoids_.end());
  }

  void clear() { ellipsoids_.clear(); }

  const std::vector<EllipsoidObject>& getEllipsoids() const {
    return ellipsoids_;
  }

  bool show() const { return !ellipsoids_.empty(); }

private:
  std::vector<EllipsoidObject> ellipsoids_;
};


struct DiskObject {
  glm::vec3 position{0.0f};
  float radius = 0.0f;
  glm::quat orientation{1,0,0,0};
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  std::string tag;

  void clear() {
    position = glm::vec3(0.0f);
    radius = 0.0f;
    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }

  void set(const glm::vec3& pos,
           float r,
           const glm::quat& rot) {
    position = pos;
    radius = r;
    orientation = rot;
  }

  glm::mat4 modelMatrix(float thicknessRatio = 0.1f) const {
    glm::mat4 M(1.0f);
    M = glm::translate(M, position);
    M = M * glm::mat4_cast(orientation);
    M = glm::scale(M, glm::vec3(radius * 2.0f,
                                radius * thicknessRatio * 2.0f,
                                radius * 2.0f));
    return M;
  }
};

class DiskManager {
public:
  DiskManager() = default;
  ~DiskManager() = default;

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  DiskManager(DiskManager&&) = default;
  DiskManager& operator=(DiskManager&&) = default;

  void add(const DiskObject& d) {
    disks_.push_back(d);
  }

  void clearGroup(const std::string& tag) {
    disks_.erase(
      std::remove_if(disks_.begin(), disks_.end(),
                     [&](const DiskObject& d){ return d.tag == tag; }),
      disks_.end());
  }

  void clear() { disks_.clear(); }

  const std::vector<DiskObject>& getDisks() const {
    return disks_;
  }

  bool show() const { return !disks_.empty(); }

private:
  std::vector<DiskObject> disks_;
};

struct LineObject {
  std::vector<glm::vec3> points;
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  std::string tag;

  void clear() {
    points.clear();
  }

  bool empty() const {
    return points.empty();
  }
};

class LineManager {
public:
  LineManager() = default;
  ~LineManager() = default;

  LineManager(const LineManager&) = delete;
  LineManager& operator=(const LineManager&) = delete;

  LineManager(LineManager&&) = default;
  LineManager& operator=(LineManager&&) = default;

  void add(const LineObject& l) {
    lines_.push_back(l);
  }

  void clearGroup(const std::string& tag) {
    lines_.erase(
      std::remove_if(lines_.begin(), lines_.end(),
                     [&](const LineObject& l){ return l.tag == tag; }),
      lines_.end());
  }

  void clear() { lines_.clear(); }

  const std::vector<LineObject>& getLines() const {
    return lines_;
  }

  bool show() const { return !lines_.empty(); }

private:
  std::vector<LineObject> lines_;
};

enum class PolyhedronRenderMode {
  Wireframe,
  Solid,
  WireframeAndSolid
};

struct PolyhedronObject {
  std::vector<glm::vec3> vertices;
  std::vector<unsigned int> lineIndices;
  std::vector<unsigned int> triIndices;

  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  PolyhedronRenderMode renderMode = PolyhedronRenderMode::Wireframe;
  std::string tag;

  void clear() {
    vertices.clear();
    lineIndices.clear();
    triIndices.clear();
  }

  bool empty() const {
    return vertices.empty();
  }

  bool hasLines() const {
    return !lineIndices.empty();
  }

  bool hasTriangles() const {
    return !triIndices.empty();
  }
};

class PolyhedronManager {
public:
  PolyhedronManager() = default;
  ~PolyhedronManager() = default;

  PolyhedronManager(const PolyhedronManager&) = delete;
  PolyhedronManager& operator=(const PolyhedronManager&) = delete;

  PolyhedronManager(PolyhedronManager&&) = default;
  PolyhedronManager& operator=(PolyhedronManager&&) = default;

  void add(int id, const PolyhedronObject& obj) {
    objects_[id] = obj;
  }

  void remove(int id) {
    objects_.erase(id);
  }

  void clear() {
    objects_.clear();
  }

  void clearGroup(const std::string& tag) {
    for (auto it = objects_.begin(); it != objects_.end(); ) {
      if (it->second.tag == tag) it = objects_.erase(it);
      else ++it;
    }
  }

  bool has(int id) const {
    return objects_.find(id) != objects_.end();
  }

  PolyhedronObject* get(int id) {
    auto it = objects_.find(id);
    return (it == objects_.end()) ? nullptr : &it->second;
  }

  const std::unordered_map<int, PolyhedronObject>& getObjects() const {
    return objects_;
  }

  bool show() const { return !objects_.empty(); }

private:
  std::unordered_map<int, PolyhedronObject> objects_;
};



struct CuboidObject {
  glm::vec3 center{0.0f};
  glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 halfSize{0.5f};

  glm::vec4 edgeColor{1.0f, 1.0f, 1.0f, 1.0f};
  std::string tag;

  bool empty() const {
    return halfSize.x <= 0.0f || halfSize.y <= 0.0f || halfSize.z <= 0.0f;
  }
};

inline std::array<glm::vec3, 8> computeCuboidCorners(const CuboidObject& obj)
{
  const glm::vec3 h = obj.halfSize;

  const std::array<glm::vec3, 8> local = {{
    {-h.x, -h.y, -h.z},
    {+h.x, -h.y, -h.z},
    {+h.x, +h.y, -h.z},
    {-h.x, +h.y, -h.z},
    {-h.x, -h.y, +h.z},
    {+h.x, -h.y, +h.z},
    {+h.x, +h.y, +h.z},
    {-h.x, +h.y, +h.z}
  }};

  std::array<glm::vec3, 8> world{};
  for (int i = 0; i < 8; ++i)
    world[i] = obj.center + obj.orientation * local[i];

  return world;
}

struct ArrowObject {
  glm::vec3 origin{0.0f};
  glm::vec3 direction{0.0f, 0.0f, 1.0f};
  float length = 1.0f;

  glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
  float headLength = 0.05f;
  float headWidth  = 0.03f;

  std::string tag;

  bool empty() const {
    return glm::length(direction) < 1.0e-8f || length <= 0.0f;
  }
};

enum class CuboidAxis {
  X = 0,
  Y = 1,
  Z = 2
};

struct CuboidAnnotationObject {
  CuboidObject cuboid;

  bool showAxisHighlight = true;
  bool showArrow = true;
  CuboidAxis selectedAxis = CuboidAxis::Z;

  glm::vec4 highlightColor{1.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 arrowColor{1.0f, 0.0f, 0.0f, 1.0f};

  float arrowLength = 0.2f;
  float arrowHeadLength = 0.05f;
  float arrowHeadWidth  = 0.03f;

  std::string tag;
};

class CuboidAnnotationManager {
public:
  void clear() { objects_.clear(); }

  void add(const CuboidAnnotationObject& obj) {
    objects_.push_back(obj);
  }

  const std::vector<CuboidAnnotationObject>& objects() const {
    return objects_;
  }

  bool show() const {
    return !objects_.empty();
  }

private:
  std::vector<CuboidAnnotationObject> objects_;
};

