#pragma once
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "core/quantity.h"

enum class DataSource : int { Gas = 0, DM = 1, Stars = 2 };
enum class StarQuantity : int { Density=0, Metallicity=1, Mass=2, Flux=3 };
enum class ProjectionVoronoiMode : int { WeightedMean = 0, OpacityRendering = 1 };
enum class ProjectionPanelLabelMode : int { Default = 0, Show = 1, Hide = 2, Override = 3 };
enum class ProjectionVectorField : int { Velocity = 0, MagneticField = 1 };
enum class ProjectionVectorOverlayMode : int { Arrows = 0, Streamlines = 1 };
enum class ProjectionVectorScaleMode : int { Linear = 0, Log = 1, Normalized = 2 };
enum class ProjectionVectorColorScaleMode : int { Linear = 0, Log = 1 };
enum class ProjectionParticleOverlayScalar : int {
  Fixed = 0,
  Mass = 1,
  Luminosity = 2,
  Density = 3,
  Metallicity = 4,
  Temperature = 5
};
enum class ProjectionParticleSizeScale : int {
  Fixed = 0,
  Linear = 1,
  Sqrt = 2,
  Log = 3,
  Saturating = 4
};
enum class ProjectionParticleSymbol : int {
  SoftCircle = 0,
  Asterisk = 1,
  FilledCircle = 2,
  Ring = 3,
  Star = 4,
  Plus = 5,
  Cross = 6,
  Diamond = 7,
  Square = 8
};

inline constexpr int kProjectionMaxViewBlocks = 4;
inline constexpr int kProjectionMaxPanels = 6;
inline constexpr int kProjectionMaxVectorOverlays = 4;
inline constexpr int kProjectionMaxStarOverlays = 4;

struct ProjectionTransferFunctionComponent {
  int type = 0; // 0=Gaussian, 1=Box, 2=Triangle.
  float center = 1.0f;
  float width = 1.0f;
  float amplitude = 1.0f;
  bool logDomain = true;
};

struct FluxSettings {
  float band_center_nm = 1500.0f;
  float band_width_nm  = 200.0f;
};

struct ProjectionViewBlockSpec {
  char name[64] = "main";
  int npixel = 200;
  float xlen[3] = {2.f, 2.f, 1.f};
  float xoffset[3] = {0.f, 0.f, 0.f};
  float tilt[3] = {0.f, 0.f, 0.f};
  int selectedAxis = 2;
  int projectionSign = 1;
  int upAxis = 1;
  int upSign = 1;
  bool sizeSameAsMain = true;
  bool centerSameAsMain = true;
  bool tiltSameAsMain = true;

  float scaleBarFractionDefault = 0.1f;
  char arrowLabelStrDefault[255] = "0.1 box";
};

struct ProjectionPanelSpec {
  int viewBlockIndex = 0;
  QuantityId quantity = QuantityId::Density;
  int colormapIndex = 0;

  bool flagLogScale = true;
  bool autoRange = true;
  float rangeMin = 0.0f;
  float rangeMax = 1.0f;

  ProjectionPanelLabelMode timeLabelMode = ProjectionPanelLabelMode::Default;
  ProjectionPanelLabelMode scaleBarMode = ProjectionPanelLabelMode::Default;
  float scaleBarFraction = 0.1f;
  char arrowLabelStr[255] = "0.1 box";

  int starOverlayIndex = 1; // 0=off, 1..kProjectionMaxStarOverlays.
  int vectorOverlayIndex = 0; // 0=off, 1..kProjectionMaxVectorOverlays.
};

struct ProjectionStarOverlaySpec {
  char name[64] = "star field 1";
  bool typeEnabled[6] = {false, false, false, true, true, true};
  float minMass = 0.0f;
  bool useMaxMass = false;
  float maxMass = 1.0f;
  ProjectionParticleOverlayScalar sizeScalar =
    ProjectionParticleOverlayScalar::Fixed;
  ProjectionParticleSizeScale sizeScale =
    ProjectionParticleSizeScale::Fixed;
  bool autoSizeRange = true;
  float sizeValueMin = 1.0f;
  float sizeValueMax = 1.0e3f;
  float minSizePx = 3.0f;
  float maxSizePx = 3.0f;
  int sizeBins = 0; // 0=continuous, >1=stepped bins.
  ProjectionParticleOverlayScalar colorScalar =
    ProjectionParticleOverlayScalar::Fixed;
  bool autoColorRange = true;
  bool colorLogScale = true;
  float colorValueMin = 1.0f;
  float colorValueMax = 1.0e3f;
  int colorColormapIndex = 0;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  ProjectionParticleSymbol symbol = ProjectionParticleSymbol::SoftCircle;
};

struct ProjectionVectorOverlaySpec {
  char name[64] = "vector field 1";
  ProjectionVectorField vectorField = ProjectionVectorField::Velocity;
  ProjectionVectorOverlayMode vectorMode = ProjectionVectorOverlayMode::Arrows;
  ProjectionVectorScaleMode vectorScaleMode = ProjectionVectorScaleMode::Linear;
  bool autoMagnitudeRange = true;
  int vectorGridSize = 32;
  float vectorMinMagnitude = 0.0f;
  float vectorMaxMagnitude = 1.0f;
  float vectorMinLengthPx = 3.0f;
  float vectorMaxLengthPx = 12.0f;
  float vectorOpacity = 0.85f;
  float vectorColor[3] = {1.0f, 1.0f, 1.0f};
  bool vectorColorByMagnitude = false;
  int vectorColormapIndex = 0;
  ProjectionVectorColorScaleMode vectorColorScaleMode =
    ProjectionVectorColorScaleMode::Linear;
  float streamlineStepPx = 4.0f;
  int streamlineMaxSteps = 200;
  int streamlineMaskSize = 64;
};

struct ProjectionMapParams {
  int npixel = 200;

  float xlen[3] = {2.f, 2.f, 1.f};
  float xoffset[3] = {0.f, 0.f, 0.f};
  float tilt[3] = {0.f, 0.f, 0.f};
  int projectionSign = 1;
  int upAxis = 1;
  int upSign = 1;

  bool flagDensityWeight = true;
  bool flagVoronoi = true;
  bool useGpuProjection = true;
  int step_z = 200;
  ProjectionVoronoiMode voronoiMode = ProjectionVoronoiMode::WeightedMean;
  bool voronoiTfLogDomain = true;
  float voronoiTfValueMin = 1.0e-6f;
  float voronoiTfValueMax = 1.0f;
  float voronoiRenderColor[3] = {0.35f, 1.0f, 0.8f};
  std::vector<ProjectionTransferFunctionComponent> voronoiTfComponents;
  QuantityId voronoiOpacityVarGas = QuantityId::Density;

  bool flagLogScale = true;
  bool autoRange = true;
  float range_min = 0.0f;
  float range_max = 1.0f;

  bool flagShowCuboid = false;

  bool flagSpecifyZoomRegionByMass = false;
  float criticalGasMassForZoomRegion = 0.0f;
  float lenZoomRegion = 0.0f;

  bool flagPlaceScale = false;
  float scaleBarFraction = 0.1f;
  char arrowLabelStr[255] = "0.1 box";

  bool flagTimeLabel = true;
  bool flagUseRedshift = false;
  char timeFormatBuf[255] = "t=%.3f";

  char fileFormat[255] = "image_%04d.png";
  char folderPath[255] = "./output";

  std::string var;

  int selectedAxis = 2;
  int selectedType = 0;
  int colormapindex = 0;

  float factorShownTimeInUnitTime = 1.0f;

  DataSource dataSource = DataSource::Gas;
  StarQuantity starQuantity = StarQuantity::Density;
  FluxSettings flux;
  float psf_sigma_pix = 1.5f;
  QuantityId selectedVarGas = QuantityId::Density;

  bool multiPanelEnabled = false;
  int multiPanelRows = 2;
  int multiPanelCols = 2;
  int multiPanelCount = 4;
  bool multiPanelShowTimeLabel[6] = {true, true, true, true, true, true};
  bool multiPanelShowScale[6] = {true, true, true, true, true, true};
  QuantityId multiPanelVars[6] = {
    QuantityId::Density,
    QuantityId::Temperature,
    QuantityId::Mass,
    QuantityId::Hsml,
    QuantityId::Density,
    QuantityId::Temperature
  };

  bool layoutInitialized = false;
  int activeViewBlockIndex = 0;
  int viewBlockCount = 1;
  int starOverlayCount = 1;
  int vectorOverlayCount = 1;
  std::array<ProjectionViewBlockSpec, kProjectionMaxViewBlocks> viewBlocks;
  std::array<ProjectionPanelSpec, kProjectionMaxPanels> panels;
  std::array<ProjectionStarOverlaySpec, kProjectionMaxStarOverlays> starOverlays;
  std::array<ProjectionVectorOverlaySpec, kProjectionMaxVectorOverlays> vectorOverlays;
};

inline void ProjectionCopyString(char* dst, std::size_t dstSize, const char* src)
{
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dstSize);
  dst[dstSize - 1] = '\0';
}

inline void ProjectionCopyVec3(float dst[3], const float src[3])
{
  for (int k = 0; k < 3; ++k) dst[k] = src[k];
}

inline int ProjectionNormalizeSign(int sign)
{
  return sign < 0 ? -1 : 1;
}

inline void ProjectionNormalizeViewBlockOrientation(ProjectionViewBlockSpec& block)
{
  block.selectedAxis = std::clamp(block.selectedAxis, 0, 2);
  block.projectionSign = ProjectionNormalizeSign(block.projectionSign);
  block.upAxis = std::clamp(block.upAxis, 0, 2);
  block.upSign = ProjectionNormalizeSign(block.upSign);
}

inline void ProjectionSetDefaultViewBlockName(ProjectionMapParams& params,
                                              int blockIndex)
{
  blockIndex = std::clamp(blockIndex, 0, kProjectionMaxViewBlocks - 1);
  if (blockIndex == 0) {
    ProjectionCopyString(params.viewBlocks[blockIndex].name,
                         sizeof(params.viewBlocks[blockIndex].name),
                         "main");
  } else {
    char name[64];
    std::snprintf(name, sizeof(name), "block %d", blockIndex);
    ProjectionCopyString(params.viewBlocks[blockIndex].name,
                         sizeof(params.viewBlocks[blockIndex].name),
                         name);
  }
}

inline void ProjectionSetDefaultStarOverlayName(ProjectionMapParams& params,
                                                int overlayIndex)
{
  overlayIndex = std::clamp(overlayIndex, 0, kProjectionMaxStarOverlays - 1);
  char name[64];
  std::snprintf(name, sizeof(name), "star field %d", overlayIndex + 1);
  ProjectionCopyString(params.starOverlays[overlayIndex].name,
                       sizeof(params.starOverlays[overlayIndex].name),
                       name);
}

inline void ProjectionSetDefaultVectorOverlayName(ProjectionMapParams& params,
                                                  int overlayIndex)
{
  overlayIndex = std::clamp(overlayIndex, 0, kProjectionMaxVectorOverlays - 1);
  char name[64];
  std::snprintf(name, sizeof(name), "vector field %d", overlayIndex + 1);
  ProjectionCopyString(params.vectorOverlays[overlayIndex].name,
                       sizeof(params.vectorOverlays[overlayIndex].name),
                       name);
}

inline void ProjectionSyncViewBlockFromTopLevel(ProjectionMapParams& params,
                                                int blockIndex)
{
  blockIndex = std::clamp(blockIndex, 0, kProjectionMaxViewBlocks - 1);
  ProjectionViewBlockSpec& block = params.viewBlocks[blockIndex];
  block.npixel = params.npixel;
  ProjectionCopyVec3(block.xlen, params.xlen);
  ProjectionCopyVec3(block.xoffset, params.xoffset);
  ProjectionCopyVec3(block.tilt, params.tilt);
  block.selectedAxis = params.selectedAxis;
  block.projectionSign = params.projectionSign;
  block.upAxis = params.upAxis;
  block.upSign = params.upSign;
  block.scaleBarFractionDefault = params.scaleBarFraction;
  ProjectionCopyString(block.arrowLabelStrDefault,
                       sizeof(block.arrowLabelStrDefault),
                       params.arrowLabelStr);
}

inline void ProjectionSyncTopLevelFromViewBlock(ProjectionMapParams& params,
                                                int blockIndex)
{
  blockIndex = std::clamp(blockIndex, 0, kProjectionMaxViewBlocks - 1);
  ProjectionViewBlockSpec block = params.viewBlocks[blockIndex];
  if (blockIndex != 0) {
    const ProjectionViewBlockSpec& main = params.viewBlocks[0];
    if (block.sizeSameAsMain) {
      ProjectionCopyVec3(block.xlen, main.xlen);
    }
    if (block.centerSameAsMain) {
      ProjectionCopyVec3(block.xoffset, main.xoffset);
    }
    if (block.tiltSameAsMain) {
      ProjectionCopyVec3(block.tilt, main.tilt);
      block.selectedAxis = main.selectedAxis;
      block.projectionSign = main.projectionSign;
      block.upAxis = main.upAxis;
      block.upSign = main.upSign;
    }
  }
  ProjectionNormalizeViewBlockOrientation(block);
  params.npixel = block.npixel;
  ProjectionCopyVec3(params.xlen, block.xlen);
  ProjectionCopyVec3(params.xoffset, block.xoffset);
  ProjectionCopyVec3(params.tilt, block.tilt);
  params.selectedAxis = block.selectedAxis;
  params.projectionSign = block.projectionSign;
  params.upAxis = block.upAxis;
  params.upSign = block.upSign;
  params.scaleBarFraction = block.scaleBarFractionDefault;
  ProjectionCopyString(params.arrowLabelStr,
                       sizeof(params.arrowLabelStr),
                       block.arrowLabelStrDefault);
}

inline ProjectionViewBlockSpec ProjectionResolveViewBlock(
  const ProjectionMapParams& params,
  int blockIndex)
{
  blockIndex = std::clamp(blockIndex, 0, kProjectionMaxViewBlocks - 1);
  ProjectionViewBlockSpec block = params.viewBlocks[blockIndex];
  if (blockIndex == 0) {
    return block;
  }

  const ProjectionViewBlockSpec& main = params.viewBlocks[0];
  if (block.sizeSameAsMain) {
    ProjectionCopyVec3(block.xlen, main.xlen);
  }
  if (block.centerSameAsMain) {
    ProjectionCopyVec3(block.xoffset, main.xoffset);
  }
  if (block.tiltSameAsMain) {
    ProjectionCopyVec3(block.tilt, main.tilt);
    block.selectedAxis = main.selectedAxis;
    block.projectionSign = main.projectionSign;
    block.upAxis = main.upAxis;
    block.upSign = main.upSign;
  }
  ProjectionNormalizeViewBlockOrientation(block);
  return block;
}

inline void ProjectionEnsureLayoutInitialized(ProjectionMapParams& params)
{
  params.viewBlockCount =
    std::clamp(params.viewBlockCount, 1, kProjectionMaxViewBlocks);
  params.multiPanelRows = std::clamp(params.multiPanelRows, 1, 3);
  params.multiPanelCols = std::clamp(params.multiPanelCols, 1, 6);
  params.multiPanelCount =
    std::min(params.multiPanelRows * params.multiPanelCols,
             kProjectionMaxPanels);
  params.starOverlayCount =
    std::clamp(params.starOverlayCount, 1, kProjectionMaxStarOverlays);
  params.vectorOverlayCount =
    std::clamp(params.vectorOverlayCount, 1, kProjectionMaxVectorOverlays);

  if (!params.layoutInitialized) {
    ProjectionSyncViewBlockFromTopLevel(params, 0);
    ProjectionSetDefaultViewBlockName(params, 0);
    for (int i = 0; i < kProjectionMaxStarOverlays; ++i) {
      ProjectionSetDefaultStarOverlayName(params, i);
    }
    for (int i = 0; i < kProjectionMaxVectorOverlays; ++i) {
      ProjectionSetDefaultVectorOverlayName(params, i);
    }
    for (int i = 0; i < kProjectionMaxPanels; ++i) {
      ProjectionPanelSpec& panel = params.panels[i];
      panel.viewBlockIndex = 0;
      panel.quantity = params.multiPanelVars[i];
      panel.colormapIndex = params.colormapindex;
      panel.flagLogScale = params.flagLogScale;
      panel.autoRange = params.autoRange;
      panel.rangeMin = params.range_min;
      panel.rangeMax = params.range_max;
      panel.timeLabelMode =
        (i < kProjectionMaxPanels && !params.multiPanelShowTimeLabel[i])
          ? ProjectionPanelLabelMode::Hide
          : ProjectionPanelLabelMode::Show;
      panel.scaleBarMode =
        (i < kProjectionMaxPanels && !params.multiPanelShowScale[i])
          ? ProjectionPanelLabelMode::Hide
          : ProjectionPanelLabelMode::Show;
      panel.scaleBarFraction = params.scaleBarFraction;
      ProjectionCopyString(panel.arrowLabelStr,
                           sizeof(panel.arrowLabelStr),
                           params.arrowLabelStr);
      panel.starOverlayIndex = 1;
    }
    params.layoutInitialized = true;
  }

  params.activeViewBlockIndex =
    std::clamp(params.activeViewBlockIndex, 0, params.viewBlockCount - 1);
  params.selectedAxis = std::clamp(params.selectedAxis, 0, 2);
  params.projectionSign = ProjectionNormalizeSign(params.projectionSign);
  params.upAxis = std::clamp(params.upAxis, 0, 2);
  params.upSign = ProjectionNormalizeSign(params.upSign);
  for (int i = 1; i < params.viewBlockCount; ++i) {
    if (std::strcmp(params.viewBlocks[i].name, "main") == 0) {
      ProjectionSetDefaultViewBlockName(params, i);
    }
  }
  for (int i = 0; i < kProjectionMaxPanels; ++i) {
    params.panels[i].viewBlockIndex =
      std::clamp(params.panels[i].viewBlockIndex,
                 0,
                 params.viewBlockCount - 1);
    params.panels[i].starOverlayIndex =
      std::clamp(params.panels[i].starOverlayIndex,
                 0,
                 params.starOverlayCount);
    params.panels[i].vectorOverlayIndex =
      std::clamp(params.panels[i].vectorOverlayIndex,
                 0,
                 params.vectorOverlayCount);
  }
  for (int i = 0; i < params.viewBlockCount; ++i) {
    ProjectionNormalizeViewBlockOrientation(params.viewBlocks[i]);
  }
  for (int i = 0; i < params.starOverlayCount; ++i) {
    if (params.starOverlays[i].name[0] == '\0') {
      ProjectionSetDefaultStarOverlayName(params, i);
    }
    ProjectionStarOverlaySpec& overlay = params.starOverlays[i];
    overlay.minMass = std::max(overlay.minMass, 0.0f);
    overlay.maxMass = std::max(overlay.maxMass, overlay.minMass);
    overlay.sizeValueMin = std::max(overlay.sizeValueMin, 0.0f);
    overlay.sizeValueMax = std::max(overlay.sizeValueMax, overlay.sizeValueMin);
    overlay.colorValueMin = std::max(overlay.colorValueMin, 0.0f);
    overlay.colorValueMax =
      std::max(overlay.colorValueMax, overlay.colorValueMin);
    overlay.minSizePx = std::max(overlay.minSizePx, 0.0f);
    overlay.maxSizePx = std::max(overlay.maxSizePx, overlay.minSizePx);
    overlay.sizeBins = std::clamp(overlay.sizeBins, 0, 64);
    overlay.opacity = std::clamp(overlay.opacity, 0.0f, 1.0f);
    overlay.symbol =
      static_cast<ProjectionParticleSymbol>(
        std::clamp(static_cast<int>(overlay.symbol), 0, 8));
  }
  for (int i = 0; i < params.vectorOverlayCount; ++i) {
    if (params.vectorOverlays[i].name[0] == '\0') {
      ProjectionSetDefaultVectorOverlayName(params, i);
    }
    ProjectionVectorOverlaySpec& overlay = params.vectorOverlays[i];
    overlay.vectorGridSize = std::clamp(overlay.vectorGridSize, 4, 256);
    overlay.vectorMinMagnitude = std::max(overlay.vectorMinMagnitude, 0.0f);
    overlay.vectorMaxMagnitude =
      std::max(overlay.vectorMaxMagnitude, overlay.vectorMinMagnitude);
    overlay.vectorMinLengthPx = std::max(overlay.vectorMinLengthPx, 0.0f);
    overlay.vectorMaxLengthPx =
      std::max(overlay.vectorMaxLengthPx, overlay.vectorMinLengthPx);
    overlay.vectorOpacity = std::clamp(overlay.vectorOpacity, 0.0f, 1.0f);
    overlay.streamlineStepPx = std::max(overlay.streamlineStepPx, 0.1f);
    overlay.streamlineMaxSteps = std::max(overlay.streamlineMaxSteps, 1);
    overlay.streamlineMaskSize =
      std::clamp(overlay.streamlineMaskSize, 8, 512);
  }
}

inline void ProjectionApplyViewBlockToParams(ProjectionMapParams& params,
                                             const ProjectionViewBlockSpec& block)
{
  params.npixel = block.npixel;
  ProjectionCopyVec3(params.xlen, block.xlen);
  ProjectionCopyVec3(params.xoffset, block.xoffset);
  ProjectionCopyVec3(params.tilt, block.tilt);
  params.selectedAxis = block.selectedAxis;
  params.projectionSign = block.projectionSign;
  params.upAxis = block.upAxis;
  params.upSign = block.upSign;
}

inline bool ProjectionResolveLabelMode(ProjectionPanelLabelMode mode,
                                       bool defaultValue)
{
  switch (mode) {
  case ProjectionPanelLabelMode::Show:
  case ProjectionPanelLabelMode::Override:
    return true;
  case ProjectionPanelLabelMode::Hide:
    return false;
  default:
    return defaultValue;
  }
}

inline void ProjectionApplyPanelToParams(ProjectionMapParams& params,
                                         const ProjectionPanelSpec& panel,
                                         const ProjectionViewBlockSpec& block)
{
  params.selectedVarGas = panel.quantity;
  params.var = QuantityLabel(panel.quantity);
  params.colormapindex = panel.colormapIndex;
  params.flagLogScale = panel.flagLogScale;
  params.autoRange = panel.autoRange;
  params.range_min = panel.rangeMin;
  params.range_max = panel.rangeMax;
  params.flagTimeLabel =
    ProjectionResolveLabelMode(panel.timeLabelMode,
                               true);
  params.flagPlaceScale =
    ProjectionResolveLabelMode(panel.scaleBarMode,
                               false);

  const bool overrideScale =
    panel.scaleBarMode == ProjectionPanelLabelMode::Override;
  params.scaleBarFraction = overrideScale
    ? panel.scaleBarFraction
    : block.scaleBarFractionDefault;
  ProjectionCopyString(params.arrowLabelStr,
                       sizeof(params.arrowLabelStr),
                       overrideScale
                         ? panel.arrowLabelStr
                         : block.arrowLabelStrDefault);
}
