#include "UI/volume_rendering_ui.h"

#ifdef VOLUME_RENDERING

#include "UI/settings_ui.h"
#include "UI/transfer_function_editor.hpp"
#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/state/runtime_state.h"
#include "app/state/ui_state.h"
#include "core/quantity.h"
#include "render/colormap_defs.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>

namespace {

struct QuantityRangeAggregate {
  float min = 0.0f;
  float max = 1.0f;
  bool valid = false;
};

struct VolumeColorPreset {
  const char* name = "";
  glm::vec3 color{1.0f, 1.0f, 1.0f};
};

const VolumeColorPreset kVolumeColorPresets[] = {
  {"Cyan plasma", {0.30f, 1.00f, 0.82f}},
  {"Ionized blue", {0.35f, 0.55f, 1.00f}},
  {"Warm gas", {1.00f, 0.62f, 0.28f}},
  {"Hydrogen red", {1.00f, 0.22f, 0.18f}},
  {"Oxygen green", {0.25f, 1.00f, 0.42f}},
  {"Magenta shock", {1.00f, 0.22f, 0.95f}},
  {"Soft white", {0.90f, 0.94f, 1.00f}},
};

QuantityRangeAggregate AggregateQuantityRange(const QuantityState& quantity,
                                              QuantityId selected,
                                              bool positiveOnly)
{
  QuantityRangeAggregate out;
  out.min = std::numeric_limits<float>::max();
  out.max = -std::numeric_limits<float>::max();

  const int qidx = static_cast<int>(selected);
  if (qidx < 0 || qidx >= kMaxQ) {
    out.min = positiveOnly ? 1.0e-6f : 0.0f;
    out.max = 1.0f;
    return out;
  }

  for (int t = 0; t < kNumTypes; ++t) {
    const float typeMin = quantity.range.valueMin[qidx][t];
    const float typeMax = quantity.range.valueMax[qidx][t];
    if (!std::isfinite(typeMin) || !std::isfinite(typeMax)) continue;
    if (typeMin == 0.0f && typeMax == 0.0f) continue;

    if (positiveOnly) {
      if (typeMax <= 0.0f) continue;
      out.min = std::min(out.min, std::max(typeMin, typeMax * 1.0e-12f));
      out.max = std::max(out.max, typeMax);
    } else {
      out.min = std::min(out.min, typeMin);
      out.max = std::max(out.max, typeMax);
    }
    out.valid = true;
  }

  if (!out.valid) {
    out.min = positiveOnly ? 1.0e-6f : 0.0f;
    out.max = 1.0f;
    return out;
  }

  if (out.max <= out.min) {
    out.max = out.min + std::max(std::abs(out.min) * 1.0e-6f, 1.0e-6f);
  }
  return out;
}

void ApplyVolumeTransferFunction(TransferFunctionEditor& editor,
                                 SettingsVolumeRenderingEdit& edit,
                                 VolumeRenderState& render)
{
  render.tfComponents.clear();
  edit.valueMin = editor.valueMin();
  edit.valueMax = editor.valueMax();
  edit.logScale = editor.logScale();
  render.tfValueMin = edit.valueMin;
  render.tfValueMax = edit.valueMax;
  render.tfSigmaScale = 1.0f;
  render.tfLogScale = edit.logScale;
  render.tfMaxSigma = 0.0f;

  if (!editor.hasComponents()) {
    return;
  }

  const auto& comps = editor.components();
  const int maxComponents =
    std::min(static_cast<int>(comps.size()), kMaxVolumeTransferComponents);
  render.tfComponents.reserve(static_cast<std::size_t>(maxComponents));
  for (int i = 0; i < maxComponents; ++i) {
    const TFComponent& src = comps[static_cast<std::size_t>(i)];
    VolumeTransferFunctionComponent dst;
    dst.type = static_cast<int>(src.type);
    dst.center = src.center;
    dst.width = src.width;
    dst.amplitude = src.amp;
    dst.logDomain = src.logDomain;
    render.tfComponents.push_back(dst);
    render.tfMaxSigma = std::max(render.tfMaxSigma,
                                 std::max(src.amp, 0.0f));
  }
}

} // namespace

void DrawVolumeRenderingSettingsSection(
  const QuantityState& quantity,
  SettingsAnalysisEditState& edit,
  const SettingsAnalysisResultView& result,
  SettingsActionRequestState& settingsReq)
{
  static TransferFunctionEditor transferEditor;
  const QuantityCatalogState& catalog = quantity.catalog;

  auto& volumeReq = edit.volume;
  auto& volumeRender = settingsReq.renderDraft.volume;
  bool volumeDirty = false;
  bool renderDirty = false;

  ImGui::SeparatorText("Volume tree");

  if (ImGui::BeginCombo("Volume quantity",
                        QuantityDisplayLabel(quantity,
                                             volumeReq.selectedQuantity))) {
    for (int q = 0; q < catalog.nUIQ; ++q) {
      QuantityId cand = catalog.uiQ[q];
      bool selected = (cand == volumeReq.selectedQuantity);
      if (ImGui::Selectable(QuantityDisplayLabel(quantity, cand), selected)) {
        volumeReq.selectedQuantity = cand;
        volumeDirty = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  volumeDirty |= ImGui::InputInt("Min particles per leaf",
                                 &volumeReq.minParticlesPerLeaf);
  if (volumeReq.minParticlesPerLeaf < 1) {
    volumeReq.minParticlesPerLeaf = 1;
    volumeDirty = true;
  }

  volumeDirty |= ImGui::SliderInt("Max tree level",
                                  &volumeReq.maxTreeLevel,
                                  1,
                                  24);

  const char* reconstructionModes[] = {
    "Cell average (fast)",
    "Shared corners",
    "Face-gradient (slow)"
  };
  volumeDirty |= ImGui::Combo("Corner reconstruction",
                              &volumeReq.cornerReconstructionMode,
                              reconstructionModes,
                              IM_ARRAYSIZE(reconstructionModes));
  volumeReq.cornerReconstructionMode =
    std::clamp(volumeReq.cornerReconstructionMode, 0, 2);

  if (ImGui::Button("Build volume tree")) {
    volumeReq.buildClicked = true;
    edit.volumeDirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear volume tree")) {
    volumeReq.clearClicked = true;
    edit.volumeDirty = true;
  }

  ImGui::SeparatorText("Transfer function");
  if (volumeReq.autoRange) {
    const QuantityRangeAggregate dataRange =
      AggregateQuantityRange(quantity,
                             volumeReq.selectedQuantity,
                             volumeReq.logScale);
    if (dataRange.valid) {
      if (volumeReq.valueMin != dataRange.min ||
          volumeReq.valueMax != dataRange.max) {
        volumeReq.valueMin = dataRange.min;
        volumeReq.valueMax = dataRange.max;
        volumeDirty = true;
      }
    }
  }
  if (volumeReq.autoRange) {
    transferEditor.set_minmax(volumeReq.selectedQuantity,
                              volumeReq.valueMin,
                              volumeReq.valueMax);
  }
  if (ImGui::Button("Open sigma-density editor")) {
    transferEditor.set_window();
  }
  if (transferEditor.showUI(nullptr)) {
    ApplyVolumeTransferFunction(transferEditor, volumeReq, volumeRender);
    volumeDirty = true;
    renderDirty = true;
  }

  volumeDirty |= ImGui::Checkbox("Log scale density mapping",
                                 &volumeReq.logScale);
  volumeDirty |= ImGui::Checkbox("Auto range",
                                 &volumeReq.autoRange);
  if (!volumeReq.autoRange) {
    volumeDirty |= ImGui::InputFloat("Value min",
                                     &volumeReq.valueMin,
                                     0.0f,
                                     0.0f,
                                     "%g");
    volumeDirty |= ImGui::InputFloat("Value max",
                                     &volumeReq.valueMax,
                                     0.0f,
                                     0.0f,
                                     "%g");
  }

  ImGui::SeparatorText("Ray marching");
  renderDirty |= ImGui::Checkbox("Show adaptive volume",
                                 &volumeRender.show);
  renderDirty |= ImGui::InputFloat("LOD pixel threshold (0=off)",
                                   &volumeRender.pixelThreshold,
                                   0.1f,
                                   1.0f,
                                   "%g");
  volumeRender.pixelThreshold = std::max(volumeRender.pixelThreshold, 0.0f);
  renderDirty |= ImGui::InputFloat("Tau max",
                                   &volumeRender.tauMax,
                                   0.1f,
                                   1.0f,
                                   "%g");
  renderDirty |= ImGui::InputFloat("Empty skip epsilon",
                                   &volumeRender.skipEpsilon,
                                   0.0f,
                                   0.0f,
                                   "%g");
  renderDirty |= ImGui::InputFloat("Sample step length (0=one per cell)",
                                   &volumeRender.stepBias,
                                   0.0f,
                                   0.0f,
                                   "%g");
  volumeRender.stepBias = std::max(volumeRender.stepBias, 0.0f);
  renderDirty |= ImGui::InputInt("Max samples per cell",
                                 &volumeRender.maxSamplesPerCell,
                                 1,
                                 8);
  volumeRender.maxSamplesPerCell =
    std::clamp(volumeRender.maxSamplesPerCell, 1, 256);

  ImGui::SeparatorText("Appearance");
  const char* colorModes[] = {
    "Fixed color",
    "Procedural heat",
    "Colormap"
  };
  renderDirty |= ImGui::Combo("Volume color mode",
                              &volumeRender.colorMode,
                              colorModes,
                              IM_ARRAYSIZE(colorModes));
  volumeRender.colorMode = std::clamp(volumeRender.colorMode, 0, 2);
  if (volumeRender.colorMode == 0) {
    const char* preview = "Preset";
    for (const VolumeColorPreset& preset : kVolumeColorPresets) {
      if (std::abs(volumeRender.baseColor.x - preset.color.x) < 1.0e-4f &&
          std::abs(volumeRender.baseColor.y - preset.color.y) < 1.0e-4f &&
          std::abs(volumeRender.baseColor.z - preset.color.z) < 1.0e-4f) {
        preview = preset.name;
        break;
      }
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Base color", preview)) {
      for (const VolumeColorPreset& preset : kVolumeColorPresets) {
        const bool selected =
          std::abs(volumeRender.baseColor.x - preset.color.x) < 1.0e-4f &&
          std::abs(volumeRender.baseColor.y - preset.color.y) < 1.0e-4f &&
          std::abs(volumeRender.baseColor.z - preset.color.z) < 1.0e-4f;
        if (ImGui::Selectable(preset.name, selected)) {
          volumeRender.baseColor = preset.color;
          renderDirty = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    renderDirty |= ImGui::ColorEdit3("##Volume base color RGB",
                                     &volumeRender.baseColor.x,
                                     ImGuiColorEditFlags_NoLabel |
                                       ImGuiColorEditFlags_Float);
  } else if (volumeRender.colorMode == 2) {
    const ColormapDef* colormaps = AvailableColormaps();
    const int colormapCount = AvailableColormapCount();
    volumeRender.colormapIndex =
      std::clamp(volumeRender.colormapIndex, 0, std::max(0, colormapCount - 1));
    const char* preview =
      colormapCount > 0 ? colormaps[volumeRender.colormapIndex].name : "None";
    if (ImGui::BeginCombo("Volume colormap", preview)) {
      for (int i = 0; i < colormapCount; ++i) {
        const bool selected = i == volumeRender.colormapIndex;
        if (ImGui::Selectable(colormaps[i].name, selected)) {
          volumeRender.colormapIndex = i;
          renderDirty = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  }

  const char* opticalModels[] = {
    "Opacity",
    "Emission",
    "Emission + absorption"
  };
  renderDirty |= ImGui::Combo("Volume optical model",
                              &volumeRender.opticalModel,
                              opticalModels,
                              IM_ARRAYSIZE(opticalModels));
  volumeRender.opticalModel = std::clamp(volumeRender.opticalModel, 0, 2);
  if (volumeRender.opticalModel != 0) {
    renderDirty |= ImGui::InputFloat("Emission scale",
                                     &volumeRender.emissionScale,
                                     0.1f,
                                     1.0f,
                                     "%g");
    volumeRender.emissionScale = std::max(volumeRender.emissionScale, 0.0f);
  }
  if (volumeRender.opticalModel == 2) {
    renderDirty |= ImGui::InputFloat("Absorption scale",
                                     &volumeRender.absorptionScale,
                                     0.1f,
                                     1.0f,
                                     "%g");
    volumeRender.absorptionScale =
      std::max(volumeRender.absorptionScale, 0.0f);
  }

  if (volumeRender.debugMode != 0) {
    volumeRender.debugMode = 0;
    renderDirty = true;
  }

  if (result.volume) {
    ImGui::SeparatorText("Last build");
    const auto& volume = *result.volume;
    ImGui::TextColored(volume.valid ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f)
                                    : ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
                       "%s",
                       volume.message.c_str());
    ImGui::Text("Nodes: %zu  Leaves: %zu  Max depth: %zu",
                volume.stats.nodeCount,
                volume.stats.leafCount,
                volume.stats.maxDepth);
    ImGui::Text("Particles: %zu  Empty dropped: %zu  Value max: %g",
                volume.stats.particleCount,
                volume.stats.emptyNodesDropped,
                volume.stats.sigmaMax);
  }

  if (volumeDirty) {
    edit.volumeDirty = true;
  }
  if (renderDirty) {
    settingsReq.renderDraftDirty = true;
    settingsReq.applyRenderRequested = true;
  }
}

#endif
