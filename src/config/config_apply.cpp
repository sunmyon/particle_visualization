#include "app/state/runtime_state.h"
#include "app/state/snapshot_state_sync.h"
#include "render/particle_visual_config.h"
#include "config/config_data.h"

void ApplyConfigData(const ConfigData& config,
                     FileNavigationRuntimeState& fileNav,
                     SnapshotFormatState& format,
                     UnitSystem& units,
		     float& desired_max,
                     ParticleVisualConfig& visual,
                     ParticleMaskConfig& mask)
{
  auto& nav = fileNav.navigation;
  auto& input = fileNav.input;

  nav.initialIndex = config.session.initialIndex;
  nav.currentFileIndex = config.session.currentFileIndex;
  nav.currentStep = config.session.currentStep;
  nav.skipStep = (config.session.skipStep > 0) ? config.session.skipStep : 1;
  nav.batchSize = config.session.batchSize;
  fileNav.tempSkipStep = nav.skipStep;

  CopySnapshotCString(input.fileFormat,
                      sizeof(input.fileFormat),
                      config.persistent.fileFormatPattern.c_str());
  CopySnapshotCString(input.folderPath,
                      sizeof(input.folderPath),
                      config.persistent.folderPath.c_str());
  RefreshSnapshotFilePath(fileNav);

  format.readFormat = config.persistent.readFormat;
  format.formatTokens = config.persistent.formatTokens;
  format.formatTokensHdf5 = config.persistent.formatTokensHdf5;
  if (!config.persistent.formatTokensGadget.empty()) {
    format.formatTokensGadget = config.persistent.formatTokensGadget;
  }
  format.outputFormat = config.persistent.outputFormat;
  if (format.outputFormat.fields.empty()) {
    format.outputFormat.fields = MakeDefaultSnapshotOutputFields();
  }
  format.inputDensityUnit = config.persistent.inputDensityUnit;
  format.inputTemperatureUnit = config.persistent.inputTemperatureUnit;
  format.inputMagneticFieldUnit = config.persistent.inputMagneticFieldUnit;

  visual = config.persistent.visual;
  mask = config.persistent.mask;

  desired_max = config.persistent.desiredMax;
  units = config.persistent.units;
}
