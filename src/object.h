#pragma once

#include <vector>
#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>   // for translate, scale
#include <glm/gtc/quaternion.hpp>         // for mat4_cast

extern int g_selectedAxis;
extern bool g_flagShowCuboid;
extern TrackingVector<glm::vec3> g_cubicPoints;

void UpdateCuboidTransformArcball(glm::vec3 &center, glm::quat &cuboidTransform
				  , float lastX, float lastY, float xpos, float ypos
				  , const glm::mat4 &view, const glm::vec3 &pivotWorld);

struct Ellipsoid {
  double a{0}, b{0}, c{0};          // 半長軸・半中軸・半短軸
  Eigen::Vector3d center{0,0,0};    // 質量中心
  Eigen::Matrix3d axes = Eigen::Matrix3d::Identity(); // 列ベクトルが主軸 (右手系)
};

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
    // Get the global instance (singleton)
    static CubeManager& getInstance() {
        static CubeManager instance;
        return instance;
    }

    // Add a cube with given position, size, optional orientation and tag
    void addCube(const glm::vec3& position,
                 const glm::vec3& size,
                 const glm::quat& orientation = glm::quat{1,0,0,0},
		 const float& opacity = 0.5,
                 const std::string& tag = "default") {
      cubes_.push_back({ position, size, orientation, opacity, tag });
    }

    void clearGroup(const std::string& tag) {
        cubes_.erase(
            std::remove_if(cubes_.begin(), cubes_.end(),
                [&](const Cube& c){ return c.tag == tag; }),
            cubes_.end());
    }
    
    // Remove cube by index (no-op if out of range)
    void removeCube(size_t index) {
        if (index < cubes_.size()) {
            cubes_.erase(cubes_.begin() + index);
        }
    }

    // Remove all cubes
    void clear() {
        cubes_.clear();
    }

    // Access the current list of cubes (read-only)
    const std::vector<Cube>& getCubes() const {
        return cubes_;
    }

  bool showCubes(){
    return (cubes_.size() > 0 ? true : false);
  }
  
private:
    CubeManager() = default;
    ~CubeManager() = default;
    CubeManager(const CubeManager&) = delete;
    CubeManager& operator=(const CubeManager&) = delete;

    std::vector<Cube> cubes_;
};

// Inline global reference to the singleton
inline CubeManager& gCubeManager = CubeManager::getInstance();
