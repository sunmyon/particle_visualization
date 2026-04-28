#include "config_extract.h"

#include "config_data.h"
#include "app/state/runtime_state.h"
#include "core/units.h"
#include "render/particle_visual_config.h"

ConfigData ExtractConfigData(const FileNavigationRuntimeState& fileNav,
                             const SnapshotFormatState& format,
                             const UnitSystem& units,
			     const float desired_max,
                             const ParticleVisualConfig& visual,
                             const ParticleMaskConfig& mask)
{
  ConfigData config;
  const auto& nav = fileNav.navigation;
  const auto& input = fileNav.input;

  config.persistent.fileFormatPattern = input.fileFormat;
  config.persistent.folderPath = input.folderPath;
  config.persistent.readFormat = format.readFormat;
  config.persistent.formatTokens = format.formatTokens;
  config.persistent.formatTokensHdf5 = format.formatTokensHdf5;
  config.persistent.visual = visual;
  config.persistent.mask = mask;
  config.persistent.desiredMax = desired_max;
  config.persistent.units = units;

  config.session.initialIndex = nav.initialIndex;
  config.session.currentFileIndex = nav.currentFileIndex;
  config.session.currentStep = nav.currentStep;
  config.session.skipStep = nav.skipStep;
  config.session.batchSize = nav.batchSize;

  return config;
}
