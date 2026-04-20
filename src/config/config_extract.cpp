#include "config_extract.h"

#include "config_data.h"
#include "FileIO/file_io.h"
#include "core/units.h"
#include "particle_visual_config.h"

ConfigData ExtractConfigData(const FileInfo& fileInfo,
                             const UnitSystem& units,
			     const float desired_max,
                             const ParticleVisualConfig& visual,
                             const ParticleMaskConfig& mask)
{
  ConfigData config;
  const auto& src = fileInfo.getSource();

  config.persistent.fileFormatPattern = src.fileFormat;
  config.persistent.folderPath = src.folderPath;
  config.persistent.readFormat = fileInfo.getFormatMode();
  config.persistent.formatTokens = src.formatTokens;
  config.persistent.formatTokensHdf5 = src.formatTokens_hdf5;
  config.persistent.visual = visual;
  config.persistent.mask = mask;
  config.persistent.normalizationFactor = desired_max;
  config.persistent.units = units;

  config.session.initialIndex = src.initialIndex;
  config.session.currentFileIndex = src.currentFileIndex;
  config.session.currentStep = src.currentStep;
  config.session.skipStep = src.skipStep;
  config.session.batchSize = src.batchSize;

  return config;
}


