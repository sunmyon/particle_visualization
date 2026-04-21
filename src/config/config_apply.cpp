#include "FileIO/file_io.h"
#include "data/particle_array.h"
#include "particle_visual_config.h"
#include "config/config_data.h"

void ApplyConfigData(const ConfigData& config,
                     FileInfo& fileInfo,
                     UnitSystem& units,
		     float& desired_max,
                     ParticleVisualConfig& visual,
                     ParticleMaskConfig& mask)
{
  auto& src = fileInfo.editSource();

  std::strncpy(src.fileFormat,
	       config.persistent.fileFormatPattern.c_str(),
	       sizeof(src.fileFormat) - 1);
  src.fileFormat[sizeof(src.fileFormat) - 1] = '\0';

  std::strncpy(src.folderPath,
	       config.persistent.folderPath.c_str(),
	       sizeof(src.folderPath) - 1);
  src.folderPath[sizeof(src.folderPath) - 1] = '\0';

  src.formatTokens = config.persistent.formatTokens;
  src.formatTokens_hdf5 = config.persistent.formatTokensHdf5;
  src.initialIndex = config.session.initialIndex;
  src.currentFileIndex = config.session.currentFileIndex;
  src.currentStep = config.session.currentStep;
  src.skipStep = config.session.skipStep;
  src.batchSize = config.session.batchSize;

  fileInfo.setFormatMode(config.persistent.readFormat);

  visual = config.persistent.visual;
  mask = config.persistent.mask;

  desired_max = config.persistent.desiredMax;
  units = config.persistent.units;
  fileInfo.setUnit(units);
}
