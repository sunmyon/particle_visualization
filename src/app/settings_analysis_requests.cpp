#include "app/settings_analysis_requests.h"

#include <algorithm>
#include <cstring>

#include <glm/gtc/quaternion.hpp>

#include "app/state/runtime_state.h"
#include "app/state/ui_state.h"

namespace {
void CopyCStr(char* dst, std::size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}

#ifdef POWER_SPECTRUM
void SyncPowerSpectrumAxisVectorFromTilt(PowerSpectrumParams& params)
{
  const glm::quat qx =
    glm::angleAxis(glm::radians(params.axisTiltDegrees[0]),
                   glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::quat qy =
    glm::angleAxis(glm::radians(params.axisTiltDegrees[1]),
                   glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::quat qz =
    glm::angleAxis(glm::radians(params.axisTiltDegrees[2]),
                   glm::vec3(0.0f, 0.0f, 1.0f));
  const glm::vec3 axis =
    glm::normalize((qz * qy * qx) * glm::vec3(0.0f, 0.0f, 1.0f));
  params.analysisAxis[0] = axis.x;
  params.analysisAxis[1] = axis.y;
  params.analysisAxis[2] = axis.z;
}
#endif
}

void SyncSettingsAnalysisDraftsFromRuntime(SettingsAnalysisEditState& edit,
                                           const AnalysisRequestState& requests)
{
  if (!edit.stellarDensityDirty) {
    for (int i = 0; i < 6; ++i) {
      edit.stellarDensity.selectedTypes[i] =
        requests.stellarDensity.selectedTypes[i];
    }
    edit.stellarDensity.overwriteHsml =
      requests.stellarDensity.overwriteHsml;
  }

#ifdef POWER_SPECTRUM
  if (!edit.powerSpectrumDirty) {
    const PowerSpectrumParams& params = requests.powerSpectrum.params;
    edit.powerSpectrum.gridSize = params.gridSize;
    edit.powerSpectrum.fieldKind = params.fieldKind;
    edit.powerSpectrum.scalarQuantity =
      params.scalarQuantity;
    edit.powerSpectrum.vectorField = params.vectorField;
    edit.powerSpectrum.subtractMean = params.subtractMean;
    edit.powerSpectrum.useRegionBox = params.useRegionBox;
    for (int i = 0; i < 3; ++i) {
      edit.powerSpectrum.regionCenter[i] =
        params.regionCenter[i];
    }
    edit.powerSpectrum.regionSideLength =
      params.regionSideLength;
    edit.powerSpectrum.regionOpacity =
      requests.powerSpectrum.preview.regionOpacity;
    edit.powerSpectrum.showRegionBox =
      requests.powerSpectrum.preview.showRegionBox;
    for (int i = 0; i < 3; ++i) {
      edit.powerSpectrum.axisTiltDegrees[i] =
        params.axisTiltDegrees[i];
      edit.powerSpectrum.analysisAxis[i] =
        params.analysisAxis[i];
    }
  }
#endif

#ifdef CLUMP_DATA_READ
  if (!edit.clumpBatchDirty) {
    edit.clumpBatch.method = requests.clumpBatch.method;
    edit.clumpBatch.nSnapshots = requests.clumpBatch.nSnapshots;
    CopyCStr(edit.clumpBatch.outputFileName,
             sizeof(edit.clumpBatch.outputFileName),
             requests.clumpBatch.outputFileName);
    CopyCStr(edit.clumpBatch.outputFolderPath,
             sizeof(edit.clumpBatch.outputFolderPath),
             requests.clumpBatch.outputFolderPath);
  }
#endif

  if (!edit.diskDirty) {
    edit.disk.targetParticleId = requests.disk.targetParticleId;
  }

  if (!edit.diskBatchDirty) {
    CopyCStr(edit.diskBatch.inputFile,
             sizeof(edit.diskBatch.inputFile),
             requests.diskBatch.inputFile);
    CopyCStr(edit.diskBatch.outputFile,
             sizeof(edit.diskBatch.outputFile),
             requests.diskBatch.outputFile);
  }

  if (!edit.ellipsoidDirty) {
    edit.ellipsoid.particleId1 = requests.ellipsoid.particleId1;
    edit.ellipsoid.particleId2 = requests.ellipsoid.particleId2;
  }

  if (!edit.ellipsoidBatchDirty) {
    CopyCStr(edit.ellipsoidBatch.inputFile,
             sizeof(edit.ellipsoidBatch.inputFile),
             requests.ellipsoidBatch.inputFile);
    CopyCStr(edit.ellipsoidBatch.outputFile,
             sizeof(edit.ellipsoidBatch.outputFile),
             requests.ellipsoidBatch.outputFile);
  }

#ifdef ISO_CONTOUR
  if (!edit.isoContourDirty) {
    edit.isoContour.isoLevel = requests.isoContour.isoLevel;
    edit.isoContour.maxTreeLevel = requests.isoContour.maxTreeLevel;
    edit.isoContour.minParticlesPerLeaf =
      requests.isoContour.minParticlesPerLeaf;
    edit.isoContour.selectedQuantity = requests.isoContour.selectedQuantity;
    edit.isoContour.cornerReconstructionMode =
      requests.isoContour.cornerReconstructionMode;
  }
#endif

#ifdef STREAM_LINE
  if (!edit.streamlinePreviewDirty) {
    for (int i = 0; i < 3; ++i) {
      edit.streamlinePreview.seedCenter[i] =
        requests.streamlinePreview.seedRegion.center[i];
      edit.streamlinePreview.seedSize[i] =
        requests.streamlinePreview.seedRegion.size[i];
    }
    edit.streamlinePreview.opacity = requests.streamlinePreview.style.opacity;
    edit.streamlinePreview.showSeedBox =
      requests.streamlinePreview.style.showSeedBox;
  }

  if (!edit.streamlineBuildDirty) {
    edit.streamlineBuild.nSeeds = requests.streamlineBuild.nSeeds;
    edit.streamlineBuild.fieldSource = requests.streamlineBuild.fieldSource;
    edit.streamlineBuild.maxSteps = requests.streamlineBuild.maxSteps;
    edit.streamlineBuild.stepScale = requests.streamlineBuild.stepScale;
    edit.streamlineBuild.thetaMaxDegrees =
      requests.streamlineBuild.thetaMaxDegrees;
    edit.streamlineBuild.useManualSeed =
      requests.streamlineBuild.useManualSeed;
    edit.streamlineBuild.manualSeeds =
      requests.streamlineBuild.manualSeeds;
    edit.streamlineBuild.limitRegion = requests.streamlineBuild.limitRegion;
    for (int i = 0; i < 3; ++i) {
      edit.streamlineBuild.regionCenter[i] =
        requests.streamlineBuild.regionCenter[i];
      edit.streamlineBuild.regionSize[i] =
        requests.streamlineBuild.regionSize[i];
    }
  }
#endif

#ifdef VOLUME_RENDERING
  if (!edit.volumeDirty) {
    edit.volume.selectedQuantity = requests.volume.selectedQuantity;
    edit.volume.minParticlesPerLeaf = requests.volume.minParticlesPerLeaf;
    edit.volume.maxTreeLevel = requests.volume.maxTreeLevel;
    edit.volume.logScale = requests.volume.logScale;
    edit.volume.autoRange = requests.volume.autoRange;
    edit.volume.valueMin = requests.volume.valueMin;
    edit.volume.valueMax = requests.volume.valueMax;
    edit.volume.cornerReconstructionMode =
      requests.volume.cornerReconstructionMode;
  }
#endif

  if (!edit.projectionMovieDirty) {
    edit.projectionMovie.nSnapshots = requests.projectionMovie.nSnapshots;
    CopyCStr(edit.projectionMovie.outputFileFormat,
             sizeof(edit.projectionMovie.outputFileFormat),
             requests.projectionMovie.outputFileFormat);
    CopyCStr(edit.projectionMovie.outputFolderPath,
             sizeof(edit.projectionMovie.outputFolderPath),
             requests.projectionMovie.outputFolderPath);
    CopyCStr(edit.projectionMovie.outputMovieName,
             sizeof(edit.projectionMovie.outputMovieName),
             requests.projectionMovie.outputMovieName);

    edit.projectionMovie.faceOn = requests.projectionMovie.faceOn;
    edit.projectionMovie.alignToAngularMomentum =
      requests.projectionMovie.alignToAngularMomentum;
    edit.projectionMovie.amViewMode = requests.projectionMovie.amViewMode;
    edit.projectionMovie.amRadius = requests.projectionMovie.amRadius;
    edit.projectionMovie.amSubtractBulkVelocity =
      requests.projectionMovie.amSubtractBulkVelocity;
    edit.projectionMovie.amUseType = requests.projectionMovie.amUseType;
    edit.projectionMovie.amKeepSignContinuity =
      requests.projectionMovie.amKeepSignContinuity;

    edit.projectionMovie.followSinkCenter =
      requests.projectionMovie.followSinkCenter;
    edit.projectionMovie.followMostMassiveSink =
      requests.projectionMovie.followMostMassiveSink;
    edit.projectionMovie.particleIdCenter =
      requests.projectionMovie.particleIdCenter;
    edit.projectionMovie.useMassCenter =
      requests.projectionMovie.useMassCenter;
    edit.projectionMovie.massCenterRadius =
      requests.projectionMovie.massCenterRadius;
    edit.projectionMovie.massCenterMinDensity =
      requests.projectionMovie.massCenterMinDensity;
    edit.projectionMovie.restoreCameraOnFinish =
      requests.projectionMovie.restoreCameraOnFinish;
  }
}

namespace {

void SubmitStellarDensityRequest(SettingsStellarDensityEdit& edit,
                                 bool& dirty,
                                 StellarDensityRequestState& request)
{
  if (!dirty && !edit.computeClicked) return;

  for (int i = 0; i < 6; ++i) {
    request.selectedTypes[i] = edit.selectedTypes[i];
  }
  request.overwriteHsml = edit.overwriteHsml;
  request.runRequested = edit.computeClicked;

  edit.computeClicked = false;
  dirty = false;
}

#ifdef POWER_SPECTRUM
void SubmitPowerSpectrumRequest(SettingsPowerSpectrumEdit& edit,
                                bool& dirty,
                                PowerSpectrumRequestState& request)
{
  if (!dirty && !edit.computeClicked && !edit.clearClicked) return;

  PowerSpectrumParams& params = request.params;
  params.gridSize = std::clamp(edit.gridSize, 8, 256);
  params.fieldKind = std::clamp(edit.fieldKind, 0, 1);
  params.scalarQuantity = edit.scalarQuantity;
  params.vectorField = std::clamp(edit.vectorField, 0, 1);
  params.subtractMean = edit.subtractMean;
  params.useRegionBox = edit.useRegionBox;
  for (int i = 0; i < 3; ++i) {
    params.regionCenter[i] = edit.regionCenter[i];
  }
  params.regionSideLength = std::max(0.0f, edit.regionSideLength);
  request.preview.regionOpacity = std::clamp(edit.regionOpacity, 0.0f, 1.0f);
  request.preview.showRegionBox = edit.showRegionBox;
  for (int i = 0; i < 3; ++i) {
    params.axisTiltDegrees[i] = edit.axisTiltDegrees[i];
    params.analysisAxis[i] = edit.analysisAxis[i];
  }
  request.setAxisFromAngularMomentumRequested =
    edit.setAxisFromAngularMomentumClicked;
  if (!request.setAxisFromAngularMomentumRequested) {
    SyncPowerSpectrumAxisVectorFromTilt(params);
    for (int i = 0; i < 3; ++i) {
      edit.analysisAxis[i] = params.analysisAxis[i];
    }
  }
  request.runRequested = edit.computeClicked;
  request.clearRequested = edit.clearClicked;
  request.regionUpdateRequested = true;

  edit.gridSize = params.gridSize;
  edit.fieldKind = params.fieldKind;
  edit.vectorField = params.vectorField;
  edit.regionSideLength = params.regionSideLength;
  edit.regionOpacity = request.preview.regionOpacity;
  edit.setAxisFromAngularMomentumClicked = false;
  edit.computeClicked = false;
  edit.clearClicked = false;
  dirty = false;
}
#endif

#ifdef CLUMP_DATA_READ
void SubmitClumpBatchRequest(SettingsClumpBatchEdit& edit,
                             bool& dirty,
                             ClumpBatchRequestState& request)
{
  if (!dirty && !edit.generateClicked && !edit.cancelClicked) return;

  if (dirty || edit.generateClicked) {
    request.method = edit.method;
    request.nSnapshots = edit.nSnapshots;
    CopyCStr(request.outputFileName,
             sizeof(request.outputFileName),
             edit.outputFileName);
    CopyCStr(request.outputFolderPath,
             sizeof(request.outputFolderPath),
             edit.outputFolderPath);
  }
  request.runRequested = edit.generateClicked;
  request.cancelRequested = edit.cancelClicked;

  edit.generateClicked = false;
  edit.cancelClicked = false;
  dirty = false;
}
#endif

void SubmitDiskAnalysisRequest(SettingsDiskAnalysisEdit& edit,
                               bool& dirty,
                               DiskAnalysisRequestState& request)
{
  if (!dirty && !edit.findClicked && !edit.clearClicked) return;

  request.targetParticleId = edit.targetParticleId;
  request.runRequested = edit.findClicked;
  request.clearRequested = edit.clearClicked;

  edit.findClicked = false;
  edit.clearClicked = false;
  dirty = false;
}

void SubmitDiskBatchRequest(SettingsDiskBatchEdit& edit,
                            bool& dirty,
                            DiskAnalysisBatchRequestState& request)
{
  if (!dirty && !edit.runClicked && !edit.cancelClicked) return;

  if (dirty || edit.runClicked) {
    CopyCStr(request.inputFile,
             sizeof(request.inputFile),
             edit.inputFile);
    CopyCStr(request.outputFile,
             sizeof(request.outputFile),
             edit.outputFile);
  }
  request.runRequested = edit.runClicked;
  request.cancelRequested = edit.cancelClicked;

  edit.runClicked = false;
  edit.cancelClicked = false;
  dirty = false;
}

void SubmitEllipsoidAnalysisRequest(SettingsEllipsoidAnalysisEdit& edit,
                                    bool& dirty,
                                    EllipsoidAnalysisRequestState& request)
{
  if (!dirty && !edit.fitClicked && !edit.clearClicked) return;

  request.particleId1 = edit.particleId1;
  request.particleId2 = edit.particleId2;
  request.runRequested = edit.fitClicked;
  request.clearRequested = edit.clearClicked;

  edit.fitClicked = false;
  edit.clearClicked = false;
  dirty = false;
}

void SubmitEllipsoidBatchRequest(SettingsEllipsoidBatchEdit& edit,
                                 bool& dirty,
                                 EllipsoidAnalysisBatchRequestState& request)
{
  if (!dirty && !edit.runClicked && !edit.cancelClicked) return;

  if (dirty || edit.runClicked) {
    CopyCStr(request.inputFile,
             sizeof(request.inputFile),
             edit.inputFile);
    CopyCStr(request.outputFile,
             sizeof(request.outputFile),
             edit.outputFile);
  }
  request.runRequested = edit.runClicked;
  request.cancelRequested = edit.cancelClicked;

  edit.runClicked = false;
  edit.cancelClicked = false;
  dirty = false;
}

#ifdef ISO_CONTOUR
void SubmitIsoContourRequest(SettingsIsoContourEdit& edit,
                             bool& dirty,
                             IsoContourRequestState& request)
{
  if (!dirty && !edit.buildClicked && !edit.applyClicked && !edit.clearClicked) return;

  request.isoLevel = edit.isoLevel;
  request.maxTreeLevel = edit.maxTreeLevel;
  request.minParticlesPerLeaf = edit.minParticlesPerLeaf;
  request.selectedQuantity = edit.selectedQuantity;
  request.cornerReconstructionMode = edit.cornerReconstructionMode;
  request.runRequested = edit.buildClicked;
  request.applyRequested = edit.applyClicked;
  request.clearRequested = edit.clearClicked;

  edit.buildClicked = false;
  edit.applyClicked = false;
  edit.clearClicked = false;
  dirty = false;
}
#endif

#ifdef STREAM_LINE
void SubmitStreamlinePreviewRequest(SettingsStreamlinePreviewEdit& edit,
                                    bool& dirty,
                                    StreamlinePreviewRequestState& request)
{
  if (!dirty && !edit.updateClicked && !edit.clearClicked) return;

  for (int i = 0; i < 3; ++i) {
    request.seedRegion.center[i] = edit.seedCenter[i];
    request.seedRegion.size[i] = edit.seedSize[i];
  }
  request.style.opacity = edit.opacity;
  request.style.showSeedBox = edit.showSeedBox;
  request.updateRequested = edit.updateClicked;
  request.clearRequested = edit.clearClicked;

  edit.updateClicked = false;
  edit.clearClicked = false;
  dirty = false;
}

void SubmitStreamlineBuildRequest(const SettingsStreamlinePreviewEdit& preview,
                                  SettingsStreamlineBuildEdit& edit,
                                  bool& dirty,
                                  StreamlineBuildRequestState& request)
{
  if (!dirty && !edit.buildClicked && !edit.clearClicked) return;

  request.nSeeds = edit.nSeeds;
  request.fieldSource = edit.fieldSource;
  request.maxSteps = edit.maxSteps;
  request.stepScale = edit.stepScale;
  request.thetaMaxDegrees = edit.thetaMaxDegrees;
  request.useManualSeed = edit.useManualSeed;
  request.manualSeeds = edit.manualSeeds;
  for (int i = 0; i < 3; ++i) {
    request.seedCenter[i] = preview.seedCenter[i];
    request.seedSize[i] = preview.seedSize[i];
  }
  request.limitRegion = edit.limitRegion;
  for (int i = 0; i < 3; ++i) {
    request.regionCenter[i] =
      edit.limitRegion ? edit.regionCenter[i] : preview.seedCenter[i];
    request.regionSize[i] =
      edit.limitRegion ? edit.regionSize[i] : preview.seedSize[i];
  }
  request.runRequested = edit.buildClicked;
  request.clearRequested = edit.clearClicked;

  edit.buildClicked = false;
  edit.clearClicked = false;
  dirty = false;
}
#endif

#ifdef VOLUME_RENDERING
void SubmitVolumeRenderingRequest(SettingsVolumeRenderingEdit& edit,
                                  bool& dirty,
                                  VolumeRenderingRequestState& request)
{
  if (!dirty && !edit.buildClicked && !edit.clearClicked) return;

  request.selectedQuantity = edit.selectedQuantity;
  request.minParticlesPerLeaf = edit.minParticlesPerLeaf;
  request.maxTreeLevel = edit.maxTreeLevel;
  request.logScale = edit.logScale;
  request.autoRange = edit.autoRange;
  request.valueMin = edit.valueMin;
  request.valueMax = edit.valueMax;
  request.cornerReconstructionMode = edit.cornerReconstructionMode;
  request.buildRequested = edit.buildClicked;
  request.clearRequested = edit.clearClicked;

  edit.buildClicked = false;
  edit.clearClicked = false;
  dirty = false;
}
#endif

#ifdef PYTHON_BRIDGE
void SubmitPythonBridgeRequest(SettingsPythonBridgeEdit& edit,
                               bool& dirty,
                               PythonBridgeRequestState& request)
{
  if (!dirty &&
      !edit.launchClicked &&
      !edit.shutdownClicked &&
      !edit.openBrowserClicked) {
    return;
  }

  request.launchRequested = edit.launchClicked;
  request.shutdownRequested = edit.shutdownClicked;
  request.openBrowserRequested = edit.openBrowserClicked;

  edit.launchClicked = false;
  edit.shutdownClicked = false;
  edit.openBrowserClicked = false;
  dirty = false;
}
#endif

void SubmitProjectionMovieRequest(SettingsProjectionMovieEdit& edit,
                                  bool& dirty,
                                  ProjectionMovieRequestState& request,
                                  const ProjectionMapParams* projectionMapParams)
{
  if (!dirty && !edit.generateClicked && !edit.cancelClicked) return;

  if (dirty || edit.generateClicked) {
    request.nSnapshots = edit.nSnapshots;
    CopyCStr(request.outputFileFormat,
             sizeof(request.outputFileFormat),
             edit.outputFileFormat);
    CopyCStr(request.outputFolderPath,
             sizeof(request.outputFolderPath),
             edit.outputFolderPath);
    CopyCStr(request.outputMovieName,
             sizeof(request.outputMovieName),
             edit.outputMovieName);

    request.faceOn = edit.faceOn;
    request.alignToAngularMomentum = edit.alignToAngularMomentum;
    request.amViewMode = edit.amViewMode;
    request.amRadius = edit.amRadius;
    request.amSubtractBulkVelocity = edit.amSubtractBulkVelocity;
    request.amUseType = edit.amUseType;
    request.amKeepSignContinuity = edit.amKeepSignContinuity;

    request.followSinkCenter = edit.followSinkCenter;
    request.followMostMassiveSink = edit.followMostMassiveSink;
    request.particleIdCenter = edit.particleIdCenter;
    request.useMassCenter = edit.useMassCenter;
    request.massCenterRadius = edit.massCenterRadius;
    request.massCenterMinDensity = edit.massCenterMinDensity;

    request.restoreCameraOnFinish = edit.restoreCameraOnFinish;
  }

  if (edit.generateClicked && projectionMapParams) {
    request.projectionParams = *projectionMapParams;
    request.hasProjectionParams = true;
  }

  request.runRequested = edit.generateClicked;
  request.cancelRequested = edit.cancelClicked;

  edit.generateClicked = false;
  edit.cancelClicked = false;
  dirty = false;
}
}

void SubmitSettingsAnalysisRequests(SettingsAnalysisEditState& edit,
                                    AnalysisRequestState& requests,
                                    const ProjectionMapParams* projectionMapParams)
{
  SubmitStellarDensityRequest(edit.stellarDensity,
                              edit.stellarDensityDirty,
                              requests.stellarDensity);

#ifdef POWER_SPECTRUM
  SubmitPowerSpectrumRequest(edit.powerSpectrum,
                             edit.powerSpectrumDirty,
                             requests.powerSpectrum);
#endif

#ifdef CLUMP_DATA_READ
  SubmitClumpBatchRequest(edit.clumpBatch,
                          edit.clumpBatchDirty,
                          requests.clumpBatch);
#endif

  SubmitDiskAnalysisRequest(edit.disk,
                            edit.diskDirty,
                            requests.disk);
  SubmitDiskBatchRequest(edit.diskBatch,
                         edit.diskBatchDirty,
                         requests.diskBatch);
  SubmitEllipsoidAnalysisRequest(edit.ellipsoid,
                                 edit.ellipsoidDirty,
                                 requests.ellipsoid);
  SubmitEllipsoidBatchRequest(edit.ellipsoidBatch,
                              edit.ellipsoidBatchDirty,
                              requests.ellipsoidBatch);

#ifdef ISO_CONTOUR
  SubmitIsoContourRequest(edit.isoContour,
                          edit.isoContourDirty,
                          requests.isoContour);
#endif

#ifdef STREAM_LINE
  SubmitStreamlinePreviewRequest(edit.streamlinePreview,
                                 edit.streamlinePreviewDirty,
                                 requests.streamlinePreview);
  SubmitStreamlineBuildRequest(edit.streamlinePreview,
                               edit.streamlineBuild,
                               edit.streamlineBuildDirty,
                               requests.streamlineBuild);
#endif

#ifdef VOLUME_RENDERING
  SubmitVolumeRenderingRequest(edit.volume,
                               edit.volumeDirty,
                               requests.volume);
#endif

#ifdef PYTHON_BRIDGE
  SubmitPythonBridgeRequest(edit.py,
                            edit.pyDirty,
                            requests.py);
#endif

  SubmitProjectionMovieRequest(edit.projectionMovie,
                               edit.projectionMovieDirty,
                               requests.projectionMovie,
                               projectionMapParams);
}
