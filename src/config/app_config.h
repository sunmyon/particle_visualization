#pragma once

#include <array>
#include <string>
#include <vector>

struct FileConfig {
  std::string currentPath;
  std::string currentHaloCatalogPath;
};

struct CameraConfig {
  std::array<float, 3> cameraPos    = {0.f, 0.f, 10.f};
  std::array<float, 3> cameraTarget = {0.f, 0.f, 0.f};
  std::array<float, 3> cameraUp     = {0.f, 1.f, 0.f};
};

struct ParticleFollowConfig {
  bool followParticleID = false;
  int  targetParticleID = -1;

  bool followClumpCenter = false;
};

struct ParticleDisplayConfig {
  bool drawType[6] = {true, true, true, true, true, true};

  bool showVelocityVectors = false;
  int  velocitySubtraction = 1;
  float arrowScale = 1.0f;
  bool useVelocityArrowLogScale = false;
};

struct ProjectionConfig {
  bool enabled = false;

  int npixel = 200;
  std::array<float, 3> xlen    = {2.f, 2.f, 1.f};
  std::array<float, 3> xoffset = {0.f, 0.f, 0.f};
  std::array<float, 3> tilt    = {0.f, 0.f, 0.f};

  int selectedAxis = 2;
  int selectedType = 0;

  bool flagLogScale = true;
  bool autoRange = true;
  float rangeMin = 0.0f;
  float rangeMax = 1.0f;

  bool flagDensityWeight = true;
  bool flagVoronoi = true;
  int stepZ = 200;
};

struct MaskConfig {
  // 粒子 type 単位の mask
  bool typeEnabled[6] = {true, true, true, true, true, true};

  // camera 半径 filter のような UI 由来条件
  bool useCameraCenterFilter = false;
  float cameraRadius = 10.0f;

  // halo stress / clump / 明示選択など、後で足せるように
  std::vector<int> stressedHaloIndices;
  std::vector<int> selectedParticleIDs;

  // 粒子ごとの flag_mask を直接保存したい場合の最小版
  // 大きくなりやすいので optional 扱い
  bool saveExplicitMask = false;
  std::vector<int> explicitMaskedParticleIDs;
};

struct AppConfig {
  int version = 1;

  FileConfig file;
  CameraConfig camera;
  ParticleFollowConfig follow;
  ParticleDisplayConfig display;
  ProjectionConfig projection;
  MaskConfig mask;
};
