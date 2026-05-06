#include "app/app_snapshot_load.h"

#include "app/state/app_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/app_services.h"
#include "FileIO/snapshot_io_service.h"
#include "data/simulation_block.h"
#include "data/simulation_block_validation.h"
#include "data/header_info.h"
#include "core/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static void RescaleCameraForNormalizationChange(CameraContext* camera,
                                                bool hadPreviousParticles,
                                                float oldWorldToRenderScale,
                                                float newWorldToRenderScale)
{
  if (!camera || !hadPreviousParticles) return;
  if (!std::isfinite(oldWorldToRenderScale) || oldWorldToRenderScale <= 0.0f) return;
  if (!std::isfinite(newWorldToRenderScale) || newWorldToRenderScale <= 0.0f) return;

  const float ratio = newWorldToRenderScale / oldWorldToRenderScale;
  if (!std::isfinite(ratio) || ratio <= 0.0f || ratio == 1.0f) return;

  camera->cameraPos *= ratio;
  camera->cameraTarget *= ratio;
  camera->distance *= ratio;
}

static void MarkPostSnapshotLoad(SnapshotPostprocessState& post)
{
  post.refreshTree = true;
  post.refreshCulling = true;
  post.refreshTopParticles = true;
  post.applyTrackingToCamera = true;
}

static void CopyCStr(char* dst, std::size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}

static SnapshotLoadParams BuildSnapshotLoadParams(const AppRuntimeState& runtime)
{
  SnapshotLoadParams p;
  const auto& nav = runtime.settings.fileNavigation.navigation;
  const auto& input = runtime.settings.fileNavigation.input;
  const auto& fmt = runtime.settings.snapshotFormat;

  p.initialIndex = nav.initialIndex;
  p.currentFileIndex = nav.currentFileIndex;
  p.batchSize = nav.batchSize;
  p.skipStep = nav.skipStep;
  p.currentStep = nav.currentStep;

  CopyCStr(p.fileFormat, sizeof(p.fileFormat), input.fileFormat);
  CopyCStr(p.folderPath, sizeof(p.folderPath), input.folderPath);
  CopyCStr(p.filePath, sizeof(p.filePath), input.filePath);
#ifdef HAVE_HDF5
  p.useHDF5 = input.useHDF5;
#endif

  p.readFormat = fmt.readFormat;
  p.formatTokens = fmt.formatTokens;
  p.formatTokensHdf5 = fmt.formatTokensHdf5;
  p.formatTokensGadget = fmt.formatTokensGadget;
  p.inputDensityUnit = fmt.inputDensityUnit;
  p.inputTemperatureUnit = fmt.inputTemperatureUnit;
  p.inputMagneticFieldUnit = fmt.inputMagneticFieldUnit;
  p.units = runtime.quantity.units;
  return p;
}

static bool EstimateCosmicTimeGyr(const HeaderInfo& header,
                                  double scaleFactor,
                                  double unitHubble,
                                  double& outCosmicTimeGyr)
{
  if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0) {
    return false;
  }

  double omegaM = header.Omega0;
  double omegaL = header.OmegaLambda;
  if (!std::isfinite(omegaM) || !std::isfinite(omegaL) || omegaM < 0.0 || omegaL < 0.0 ||
      (omegaM == 0.0 && omegaL == 0.0)) {
    // Fallback cosmology when snapshot metadata is incomplete.
    omegaM = 0.3;
    omegaL = 0.7;
  }

  double h = (header.HubbleParam > 0.0 && std::isfinite(header.HubbleParam))
    ? header.HubbleParam
    : unitHubble;
  if (!std::isfinite(h) || h <= 0.0) {
    h = 0.7;
  }
  if (!std::isfinite(h) || h <= 0.0) {
    return false;
  }

  const double omegaK = 1.0 - omegaM - omegaL;
  auto invAEdA = [&](double a) -> double {
    if (a <= 0.0) return 0.0;
    const double e2 = omegaM / (a * a * a) + omegaK / (a * a) + omegaL;
    if (!std::isfinite(e2) || e2 <= 0.0) {
      return 0.0;
    }
    return 1.0 / (a * std::sqrt(e2));
  };

  const double aMin = 1.0e-6;
  const int nStep = 1024;

  double integral = 0.0;
  if (scaleFactor <= aMin) {
    if (omegaM <= 0.0) return false;
    integral = (2.0 / 3.0) * std::pow(scaleFactor, 1.5) / std::sqrt(omegaM);
  } else {
    if (omegaM > 0.0) {
      integral += (2.0 / 3.0) * std::pow(aMin, 1.5) / std::sqrt(omegaM);
    }

    const double a0 = aMin;
    const double a1 = scaleFactor;
    const double hStep = (a1 - a0) / static_cast<double>(nStep);
    double simpson = invAEdA(a0) + invAEdA(a1);
    for (int i = 1; i < nStep; ++i) {
      const double x = a0 + hStep * static_cast<double>(i);
      simpson += ((i % 2 == 0) ? 2.0 : 4.0) * invAEdA(x);
    }
    integral += simpson * hStep / 3.0;
  }

  if (!std::isfinite(integral) || integral <= 0.0) {
    return false;
  }

  const double hubbleTimeGyr =
    physics_constants::Mpc_cm /
    (1.0e7 * h) /
    physics_constants::year_in_sec /
    1.0e9;

  outCosmicTimeGyr = integral * hubbleTimeGyr;
  return std::isfinite(outCosmicTimeGyr) && outCosmicTimeGyr > 0.0;
}

static void UpdateSnapshotCurrentState(const HeaderInfo& header,
                                       const UnitSystem& units,
                                       SnapshotCurrentState& current);

static void GenerateTestDataSnapshot(AppDataState& data,
                                     AppRuntimeState& runtime)
{
  HeaderInfo header;
  SimulationBlock block = SimulationBlock::makeTestSimulationBlock(header);
  SimulationBlock oldBlock;
  data.particles->setSimulationBlock(std::move(block),
                                   &oldBlock,
                                   header,
                                   runtime.settings.normalization,
				   runtime.quantity);
  UpdateSnapshotCurrentState(header, runtime.quantity.units, runtime.settings.fileNavigation.current);
  runtime.settings.fileNavigation.current.loadedParticleCount =
    data.particles->simulationBlock.size();
}

static void MarkSnapshotLoadFailure(SnapshotLoadRuntimeState& load,
                                    SnapshotLoadOwner owner,
                                    int targetStep,
                                    const char* message)
{
  load.result.failedThisFrame = true;
  load.result.loadedStep = targetStep;
  load.result.owner = owner;
  std::snprintf(load.result.errorMessage,
                sizeof(load.result.errorMessage),
                "%s",
                message ? message : "snapshot load failed");
}

static void UpdateSnapshotCurrentState(const HeaderInfo& header,
                                       const UnitSystem& units,
                                       SnapshotCurrentState& current)
{
  current.loadedTime = header.time;
  current.useComovingCoordinates = units.useComovingCoordinate;

  double scaleFactor = 1.0;
  if (units.useComovingCoordinate) {
    scaleFactor = header.time;
    if ((!std::isfinite(scaleFactor) || scaleFactor <= 0.0) &&
        header.has_redshift &&
        std::isfinite(header.redshift) &&
        header.redshift > -1.0) {
      scaleFactor = 1.0 / (1.0 + header.redshift);
    }
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0) {
      scaleFactor = 1.0;
    }
  }
  current.loadedScaleFactor = scaleFactor;

  if (header.has_redshift) {
    current.loadedRedshift = header.redshift;
  } else if (scaleFactor > 0.0) {
    current.loadedRedshift = (1.0 / scaleFactor) - 1.0;
  } else {
    current.loadedRedshift = 0.0;
  }

  if (units.useComovingCoordinate &&
      EstimateCosmicTimeGyr(header, scaleFactor, units.hubble, current.loadedCosmicTime)) {
    current.hasCosmicTime = true;
  } else {
    current.loadedCosmicTime = 0.0;
    current.hasCosmicTime = false;
  }
}

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime,
                              AppServices& services,
                              CameraContext* camera)
{
  runtime.snapshotLoad.result = SnapshotLoadResultState{};

  auto& req = runtime.snapshotLoad.request;
  if (!req.pending) return;
  if (!services.snapshotIO) return;
  if (services.snapshotIO->isLoading()) return;

  auto& fileNav = runtime.settings.fileNavigation;
  auto& nav = fileNav.navigation;
  const SnapshotNavigationState previousNavigation = nav;
  const bool hadPreviousParticles =
    data.particles && !data.particles->simulationBlock.particles.empty();
  const float oldWorldToRenderScale =
    data.particles ? data.particles->simulationBlock.worldToRenderScale : 1.0f;
  nav.currentStep = req.targetStep;
  RecomputeCurrentFileIndex(fileNav);
  const int newFileIndex = nav.currentFileIndex;

  if (req.kind == SnapshotLoadKind::GenerateTestData) {
    GenerateTestDataSnapshot(data, runtime);
  } else {
    const SnapshotLoadParams params = BuildSnapshotLoadParams(runtime);
    SnapshotReadResult loaded;
    const bool ok = services.snapshotIO->loadNewSnapshot(newFileIndex,
                                                         params,
                                                         loaded,
                                                         runtime.settings.inputFilter);
    if (!ok) {
      nav = previousNavigation;
      MarkSnapshotLoadFailure(runtime.snapshotLoad,
                              req.owner,
                              req.targetStep,
                              "snapshot reader failed");
      req = SnapshotLoadRequestState{};
      return;
    }

    const SimulationBlockValidationResult validation =
      ValidateSimulationBlock(loaded.block);
    if (!validation.valid) {
      nav = previousNavigation;
      MarkSnapshotLoadFailure(runtime.snapshotLoad,
                              req.owner,
                              req.targetStep,
                              validation.message);
      req = SnapshotLoadRequestState{};
      return;
    }
    SimulationBlock oldBlock;
    data.particles->setSimulationBlock(std::move(loaded.block),
                                     &oldBlock,
                                     loaded.header,
                                     runtime.settings.normalization,
				     runtime.quantity);
    UpdateSnapshotCurrentState(loaded.header, runtime.quantity.units, fileNav.current);
    fileNav.current.loadedParticleCount = data.particles->simulationBlock.size();
  }

  if (data.particles) {
    RescaleCameraForNormalizationChange(camera,
                                        hadPreviousParticles,
                                        oldWorldToRenderScale,
                                        data.particles->simulationBlock.worldToRenderScale);
  }

  fileNav.current.loadedFileIndex = nav.currentFileIndex;

  runtime.quantity.conversion.displaySpace =
    runtime.quantity.units.useComovingCoordinate
    ? UnitSpace::Comoving
    : UnitSpace::Physical;
  runtime.quantity.rebuildConversion(fileNav.current.loadedScaleFactor);

  runtime.snapshotLoad.result.loadedThisFrame = true;
  runtime.snapshotLoad.result.loadedStep = nav.currentStep;
  runtime.snapshotLoad.result.owner = req.owner;

  if (req.owner == SnapshotLoadOwner::UserNavigation ||
      req.owner == SnapshotLoadOwner::ProjectionMovie) {
    MarkPostSnapshotLoad(runtime.snapshotPostprocess);
  }

  req = SnapshotLoadRequestState{};
}
