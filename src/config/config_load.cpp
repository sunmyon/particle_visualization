#include "config/config_load.h"

#include "config/config_data.h"
#include "config/config_io.h"
#include "config/config_apply.h"

bool LoadApplicationConfig(const std::string& filename,
                           FileInfo& fileInfo,
                           UnitSystem& units,
			   float& desired_max,
                           ParticleVisualConfig& visual,
                           ParticleMaskConfig& mask)
{
  ConfigData config;
  if (!LoadConfigFile(filename, config)) {
    return false;
  }

  ApplyConfigData(config, fileInfo, units, desired_max, visual, mask);
  return true;
}

bool SaveApplicationConfig(const std::string& filename,
                           FileInfo& fileInfo,
                           ParticleArray& units,
			   float &desired_max,
                           ParticleVisualConfig& visual,
                           ParticleMaskConfig& mask)
{
  ConfigData config;
  ExtractConfigData(config, fileInfo, units, desired_max, visual, mask);
  SaveConfigFile(path, config);

  return true;
}

