#pragma once

#include <string>
#include <vector>

#include "FileIO/file_format_types.h"
#include "core/units.h"
#include "data/particle_mask_config.h"
#include "render/particle_visual_config.h"

struct PersistentSettings {
  std::string fileFormatPattern = "output_%04d.dat";
  std::string folderPath = "./example/";

  FileFormat readFormat = FileFormat::Auto;
  
  std::vector<FieldSpec> formatTokens;
  std::vector<FieldSpec> formatTokensHdf5;

  ParticleVisualConfig visual;
  ParticleMaskConfig mask;

  float desiredMax = 1.0f;
  UnitSystem units;
};

struct SessionSettings {
  int initialIndex = 0;
  int currentFileIndex = 0;
  int currentStep = 0;
  int skipStep = 1;
  int batchSize = 1;
};

struct ConfigData {
  PersistentSettings persistent;
  SessionSettings session;
};
