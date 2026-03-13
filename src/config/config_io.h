#pragma once

#include <string>
#include <vector>

class ParticleArray;
class FileInfo;
struct ParticleVisualConfig;

// UI.cpp と config_io の受け渡し専用
struct ConfigMaskState {
  bool valid = false;

  bool enableSphere = false;
  double center[3] = {0.0, 0.0, 0.0};
  double radius = 0.0;

  enum class OutsideMode {
    Drop = 0,
    Thin = 1,
    KeepAll = 2
  };
  OutsideMode outsideMode = OutsideMode::KeepAll;
  unsigned long long outsideStride = 1;

  enum class TypeMode {
    Off = 0,
    On_NoThin = 1,
    On_ThinOK = 2
  };
  TypeMode typeMode[6] = {
    TypeMode::On_NoThin, TypeMode::On_NoThin, TypeMode::On_NoThin,
    TypeMode::On_NoThin, TypeMode::On_NoThin, TypeMode::On_NoThin
  };

  bool enableMaxParticles = false;
  int maxParticles = 0;
};

bool loadConfig(const std::string& filename,
                ParticleArray* P,
                FileInfo* fileInfo,
                ParticleVisualConfig* visualCfg,
                ConfigMaskState* outMaskState = nullptr);

bool saveConfig(const std::string& filename,
                const ParticleArray* P,
                const FileInfo* fileInfo,
                const ParticleVisualConfig* visualCfg,
                const ConfigMaskState* maskState = nullptr);
