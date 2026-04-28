#include "app/settings_analysis_requests.h"

#include <cstring>

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
    edit.isoContour.selectedQuantity = requests.isoContour.selectedQuantity;
  }
#endif

#ifdef STREAM_LINE
  if (!edit.streamlinePreviewDirty) {
    for (int i = 0; i < 3; ++i) {
      edit.streamlinePreview.seedCenter[i] =
        requests.streamlinePreview.seedCenter[i];
      edit.streamlinePreview.seedSize[i] =
        requests.streamlinePreview.seedSize[i];
    }
    edit.streamlinePreview.opacity = requests.streamlinePreview.opacity;
  }

  if (!edit.streamlineBuildDirty) {
    edit.streamlineBuild.nSeeds = requests.streamlineBuild.nSeeds;
    edit.streamlineBuild.thetaMaxDegrees =
      requests.streamlineBuild.thetaMaxDegrees;
    edit.streamlineBuild.useManualSeed =
      requests.streamlineBuild.useManualSeed;
    for (int i = 0; i < 3; ++i) {
      edit.streamlineBuild.manualSeed[i] =
        requests.streamlineBuild.manualSeed[i];
    }
    edit.streamlineBuild.limitRegion = requests.streamlineBuild.limitRegion;
    for (int i = 0; i < 3; ++i) {
      edit.streamlineBuild.regionCenter[i] =
        requests.streamlineBuild.regionCenter[i];
      edit.streamlineBuild.regionSize[i] =
        requests.streamlineBuild.regionSize[i];
    }
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
  if (!dirty && !edit.buildClicked && !edit.clearClicked) return;

  request.isoLevel = edit.isoLevel;
  request.maxTreeLevel = edit.maxTreeLevel;
  request.selectedQuantity = edit.selectedQuantity;
  request.runRequested = edit.buildClicked;
  request.clearRequested = edit.clearClicked;

  edit.buildClicked = false;
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
    request.seedCenter[i] = edit.seedCenter[i];
    request.seedSize[i] = edit.seedSize[i];
  }
  request.opacity = edit.opacity;
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
  request.thetaMaxDegrees = edit.thetaMaxDegrees;
  request.useManualSeed = edit.useManualSeed;
  for (int i = 0; i < 3; ++i) {
    request.manualSeed[i] = edit.manualSeed[i];
  }
  for (int i = 0; i < 3; ++i) {
    request.seedCenter[i] = preview.seedCenter[i];
    request.seedSize[i] = preview.seedSize[i];
  }
  request.limitRegion = edit.limitRegion;
  for (int i = 0; i < 3; ++i) {
    request.regionCenter[i] = edit.regionCenter[i];
    request.regionSize[i] = edit.regionSize[i];
  }
  request.runRequested = edit.buildClicked;
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
                                  ProjectionMovieRequestState& request)
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

  request.runRequested = edit.generateClicked;
  request.cancelRequested = edit.cancelClicked;

  edit.generateClicked = false;
  edit.cancelClicked = false;
  dirty = false;
}
}

void SubmitSettingsAnalysisRequests(SettingsAnalysisEditState& edit,
                                    AnalysisRequestState& requests)
{
  SubmitStellarDensityRequest(edit.stellarDensity,
                              edit.stellarDensityDirty,
                              requests.stellarDensity);

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

#ifdef PYTHON_BRIDGE
  SubmitPythonBridgeRequest(edit.py,
                            edit.pyDirty,
                            requests.py);
#endif

  SubmitProjectionMovieRequest(edit.projectionMovie,
                               edit.projectionMovieDirty,
                               requests.projectionMovie);
}
