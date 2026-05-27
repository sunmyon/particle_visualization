#include "settings_ui.h"
#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/state/runtime_state.h"
#include "app/state/ui_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/state/window_commands.h"

#include "interaction/camera.h"
#include "render/particle_visual_config.h"   // Concrete ParticleVisualConfig definition.
#include "render/render_backend.h"
#include "UI/file_format_dialog.h"
#include "UI/volume_rendering_ui.h"
#include "render/colormap_defs.h"  
#ifdef ISO_CONTOUR
#include "analysis/isosurface/iso_contour_geometry.h"
#endif

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <imgui.h>
#include <implot.h>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#else
#include "ImGuiFileDialog.h" // Match the include path.
#endif

static bool IsHDF5SnapshotPath(const char* path)
{
  if (!path || path[0] == '\0') return false;
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return ext == ".hdf5" || ext == ".h5";
}

static bool DrawTypeMaskToggleButtons(unsigned int& mask, const char* idPrefix)
{
  bool changed = false;
  ImGui::PushID(idPrefix);
  const float buttonWidth = ImGui::GetFrameHeight() * 1.35f;
  for (int type = 0; type < 6; ++type) {
    const unsigned int bit = static_cast<unsigned int>(1u << type);
    const bool selected = (mask & bit) != 0;
    if (type > 0) {
      ImGui::SameLine(0.0f, 4.0f);
    }
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    char label[8];
    std::snprintf(label, sizeof(label), "%d", type);
    if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f))) {
      if (selected) {
        mask &= ~bit;
      } else {
        mask |= bit;
      }
      changed = true;
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("PartType%d", type);
    }
  }
  ImGui::PopID();
  return changed;
}

static bool DrawFramedTypeMaskToggleButtons(unsigned int& mask,
                                            const char* idPrefix)
{
  const ImVec2 boxMin = ImGui::GetCursorScreenPos();
  ImGui::BeginGroup();
  const bool changed = DrawTypeMaskToggleButtons(mask, idPrefix);
  ImGui::EndGroup();
  const ImVec2 boxMax = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddRect(boxMin,
                                      boxMax,
                                      ImGui::GetColorU32(ImGuiCol_Border),
                                      4.0f);
  return changed;
}

struct PullDownItem {
  const char* label;
  int mode;
};

static void SyncSettingsDraftsFromRuntime(SettingsActionRequestState& request,
                                          const ParticleVisualConfig& particleVisual,
                                          const RenderRuntimeState& render,
                                          const QuantityState& quantity);
static void DrawCameraInfoSection(const SettingsCameraView& camera);
static void DrawRenderSnapshotSection(SettingsActionRequestState& request);
static void DrawPerformanceMemorySection(const SettingsMemoryView& memory,
                                         const RenderBackendCapabilities& backend,
                                         SettingsActionRequestState& req);
static void DrawParticleTypeSettingsSection(const QuantityState& quantity,
					    SettingsActionRequestState& req);
static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      WindowCommandQueue& windowCommands);
static void DrawSnapshotExtractSection(FileNavigationRuntimeState& fileNav,
                                       SnapshotFormatState& format,
                                       const SettingsCameraView& camera,
                                       const UnitSystem& units,
                                       SettingsActionRequestState& request);
static void DrawNormalizationSection(NormalizationContext& ctx,
				     SettingsActionRequestState& req);
static void DrawSinkIdSection(const SettingsCameraView& camera,
	                              SettingsActionRequestState& req);
static void DrawCameraPlacementSection(SettingsRuntimeState& rt, const SettingsCameraView& camera);
static void DrawViewCameraSection(SettingsRuntimeState& rt, const SettingsCameraView& camera);
#ifdef PYTHON_BRIDGE
static bool DrawPythonBridgeSection(SettingsPythonBridgeEdit& edit, const PythonBridgeViewState& view);
#endif

static void DrawAnalysisSection(SettingsAnalysisEditState& edit,
                                const QuantityState& quantity,
                                const SettingsCameraView& camera,
                                const AnalysisJobRuntimeState& jobs,
                                FileNavigationRuntimeState& fileNav,
                                SnapshotFormatState& snapshotFormat,
                                const UnitSystem& units,
                                const SettingsAnalysisResultView& result,
                                SettingsUIState& ui,
                                WindowCommandQueue& windowCommands,
                                SettingsActionRequestState& settingsReq);

static std::vector<FieldSpec> ExtractFieldsForCurrentFormat(
  const FileNavigationRuntimeState& fileNav,
  const SnapshotFormatState& format)
{
  switch (format.readFormat) {
  case FileFormat::HDF5:
    return format.formatTokensHdf5.empty()
      ? MakeDefaultSnapshotExtractFields()
      : format.formatTokensHdf5;
  case FileFormat::Gadget:
    return format.formatTokensGadget;
  case FileFormat::Binary:
  case FileFormat::Framed:
    return format.formatTokens;
  case FileFormat::Auto:
  default:
    if (fileNav.input.useHDF5) {
      return format.formatTokensHdf5.empty()
        ? MakeDefaultSnapshotExtractFields()
        : format.formatTokensHdf5;
    }
    return format.formatTokens;
  }
}

static void DrawRenderingSection(const QuantityState& quantity,
				 SettingsAnalysisEditState& edit,
                                 const AnalysisJobRuntimeState& jobs,
                                 const SettingsAnalysisResultView& result,
                                 const SettingsCameraView& camera,
                                 SettingsUIState& ui,
                                 WindowCommandQueue& windowCommands,
				 SettingsActionRequestState& settingsReq);

static void DrawOtherSettingsSection(SettingsRuntimeState& rt,
                                     const SettingsCameraView& camera);

void ShowSettingsUI(SettingsUIState& ui,
                    SettingsRuntimeState& settings,
                    const AnalysisJobRuntimeState& analysisJobs,
                    const RenderRuntimeState& render,
                    const ParticleVisualConfig& particleVisual,
                    const QuantityState& quantity,
                    const SettingsViewContext& view,
                    WindowCommandQueue& windowCommands) {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);

  SyncSettingsDraftsFromRuntime(settings.request,
                                particleVisual,
                                render,
                                quantity);

  DrawCameraInfoSection(view.camera);
  DrawPerformanceMemorySection(view.memory, view.backend, settings.request);
  DrawParticleTypeSettingsSection(quantity, settings.request);
  DrawFileNavigationSection(settings.fileNavigation,
                            settings.snapshotFormat,
                            view.snapshotLoading,
                            windowCommands);
  DrawViewCameraSection(settings, view.camera);
#ifdef PYTHON_BRIDGE
  if (view.pythonBridge) {
    if (DrawPythonBridgeSection(ui.analysisEdit.py, *view.pythonBridge)) {
      ui.analysisEdit.pyDirty = true;
    }
  }
#endif
  DrawAnalysisSection(ui.analysisEdit,
                      quantity,
                      view.camera,
	                      analysisJobs,
                      settings.fileNavigation,
                      settings.snapshotFormat,
                      settings.request.unitsDraft,
                      view.analysis,
                      ui,
                      windowCommands,
                      settings.request);
  DrawRenderingSection(quantity,
		                       ui.analysisEdit,
                       analysisJobs,
                       view.analysis,
                       view.camera,
                       ui,
                       windowCommands,
                       settings.request);
  DrawOtherSettingsSection(settings, view.camera);
  DrawRenderSnapshotSection(settings.request);

  ImGui::End();
}

static void SyncSettingsDraftsFromRuntime(SettingsActionRequestState& request,
                                          const ParticleVisualConfig& particleVisual,
                                          const RenderRuntimeState& render,
                                          const QuantityState& quantity)
{
  if (!request.particleVisualDraftDirty && !request.applyParticleVisualRequested) {
    request.particleVisualDraft = particleVisual;
  }

  if (!request.renderDraftDirty && !request.applyRenderRequested) {
    request.renderDraft.scheduling = render.scheduling;
    request.renderDraft.particleLabels = render.particleLabels;
    request.renderDraft.velocity = render.velocity;
#ifdef VOLUME_RENDERING
    request.renderDraft.volume = render.volume;
#endif
    request.renderDraft.diskOpacity = render.disks.opacity;
    request.renderDraft.ellipsoidOpacity = render.ellipsoids.opacity;
    request.renderDraft.isoContourOpacity = render.isocontour.opacity;
    request.renderDraft.showIsoContour = render.isocontour.show;
    request.renderDraft.showColorbar = render.colorbar.show;
    request.renderDraft.showCoordAxes = render.coordAxes.show;
    request.renderDraft.showCrossGizmo = render.crossGizmo.show;
    request.renderDraft.crossGizmoSize = render.crossGizmo.size;
  }

  if (!request.unitsDraftDirty && !request.applyUnitsRequested) {
    request.unitsDraft = quantity.units;
  }
}

static void DrawCameraInfoSection(const SettingsCameraView& camera) {
  ImGui::Text("Camera Position:   (%.4g, %.4g, %.4g)",
              camera.originalPosition[0],
              camera.originalPosition[1],
              camera.originalPosition[2]);
  ImGui::Text("Camera Target:     (%.4g, %.4g, %.4g)",
              camera.originalTarget[0],
              camera.originalTarget[1],
              camera.originalTarget[2]);
}

static void DrawRenderSnapshotSection(SettingsActionRequestState& request)
{
  ImGui::Spacing();
  ImGui::BeginChild("RenderSnapshotPanel",
                    ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders |
                      ImGuiChildFlags_AutoResizeY |
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextUnformatted("Render snapshot");

  if (ImGui::Button("Save render snapshot")) {
    request.renderSnapshotRequested = true;
    request.renderSnapshotMessage = "Saving...";
  }
  ImGui::SameLine();
  if (ImGui::Button("Save movie")) {
    ImGui::OpenPopup("Volume movie settings");
  }
  if (!request.renderSnapshotMessage.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", request.renderSnapshotMessage.c_str());
  }

  ImGui::TextUnformatted("Overlays for snapshot only");
  ImGui::PushID("render_snapshot_overlays");
  ImGui::Checkbox("Colorbar", &request.renderSnapshotShowColorbar);
  ImGui::SameLine();
  ImGui::Checkbox("Axes", &request.renderSnapshotShowCoordAxes);
  ImGui::SameLine();
  ImGui::Checkbox("Cross", &request.renderSnapshotShowCrossGizmo);
  ImGui::Checkbox("Particle IDs", &request.renderSnapshotShowParticleLabels);
  ImGui::SameLine();
  ImGui::Checkbox("Time label", &request.renderSnapshotShowTimeLabel);
  ImGui::PopID();

  auto& movie = request.renderSnapshotMovie;
  if (!movie.message.empty()) {
    ImGui::TextDisabled("Movie: %s", movie.message.c_str());
  }

  if (ImGui::BeginPopupModal("Volume movie settings",
                             nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const bool running = movie.status == JobStatus::Running;
    ImGui::TextUnformatted("Save volume-rendering frames as a PNG sequence.");
    ImGui::TextDisabled("Camera is fixed for now; camera-path animation can be added later.");
    ImGui::Separator();

    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("frames##volume_render_movie", &movie.nFrames);
    movie.nFrames = std::max(movie.nFrames, 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("stride##volume_render_movie", &movie.stepStride);
    movie.stepStride = std::max(movie.stepStride, 1);
    ImGui::SetNextItemWidth(520.0f);
    ImGui::InputText("output directory##volume_render_movie",
                     movie.outputFolder,
                     IM_ARRAYSIZE(movie.outputFolder));
    ImGui::Checkbox("Rebuild volume tree per snapshot",
                    &movie.rebuildVolumeTree);
    ImGui::Checkbox("Show particles using current particle visualization",
                    &movie.showParticles);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (!movie.message.empty()) {
      ImGui::TextDisabled("%s", movie.message.c_str());
    }
    if (!running) {
      if (ImGui::Button("Start")) {
        movie.startRequested = true;
        ImGui::CloseCurrentPopup();
      }
    } else {
      if (ImGui::Button("Cancel")) {
        movie.cancelRequested = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndChild();
}

static const char* FormatBytes(size_t bytes)
{
  static char buffers[4][64];
  static int index = 0;
  char* out = buffers[index++ % 4];

  double value = static_cast<double>(bytes);
  const char* unit = "B";
  if (value >= 1024.0) {
    value /= 1024.0;
    unit = "KiB";
  }
  if (value >= 1024.0) {
    value /= 1024.0;
    unit = "MiB";
  }
  if (value >= 1024.0) {
    value /= 1024.0;
    unit = "GiB";
  }
  std::snprintf(out, 64, "%.2f %s", value, unit);
  return out;
}

#ifdef POWER_SPECTRUM
namespace {
void SyncAxisVectorFromTilt(float tiltDegrees[3], float axis[3])
{
  const glm::quat qx =
    glm::angleAxis(glm::radians(tiltDegrees[0]), glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::quat qy =
    glm::angleAxis(glm::radians(tiltDegrees[1]), glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::quat qz =
    glm::angleAxis(glm::radians(tiltDegrees[2]), glm::vec3(0.0f, 0.0f, 1.0f));
  const glm::vec3 a =
    glm::normalize((qz * qy * qx) * glm::vec3(0.0f, 0.0f, 1.0f));
  axis[0] = a.x;
  axis[1] = a.y;
  axis[2] = a.z;
}

struct PlotPositiveRange {
  double min = std::numeric_limits<double>::max();
  double max = 0.0;
  bool valid = false;

  void include(double v)
  {
    if (!std::isfinite(v) || v <= 0.0) return;
    min = std::min(min, v);
    max = std::max(max, v);
    valid = true;
  }
};

void ExpandLogRange(PlotPositiveRange& range)
{
  if (!range.valid) return;
  if (range.max <= range.min) {
    range.min *= 0.5;
    range.max *= 2.0;
  } else {
    range.min *= 0.8;
    range.max *= 1.25;
  }
  range.min = std::max(range.min, std::numeric_limits<double>::min());
}

void BuildPositivePlotSeries(const std::vector<double>& k,
                             const std::vector<double>& y,
                             std::vector<double>& plotK,
                             std::vector<double>& plotY,
                             PlotPositiveRange& xRange,
                             PlotPositiveRange& yRange)
{
  plotK.clear();
  plotY.clear();
  const std::size_t n = std::min(k.size(), y.size());
  plotK.reserve(n);
  plotY.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isfinite(k[i]) || !std::isfinite(y[i]) ||
        k[i] <= 0.0 || y[i] <= 0.0) {
      continue;
    }
    plotK.push_back(k[i]);
    plotY.push_back(y[i]);
    xRange.include(k[i]);
    yRange.include(y[i]);
  }
}
}  // namespace
#endif

static void DrawMemoryPressureWarning(const char* label,
                                      size_t estimate,
                                      bool availableKnown,
                                      size_t available)
{
  if (!availableKnown || available == 0) {
    ImGui::Text("%s available: unknown", label);
    return;
  }

  const double ratio = static_cast<double>(estimate) /
                       static_cast<double>(available);
  ImGui::Text("%s available: %s", label, FormatBytes(available));
  if (ratio >= 0.9) {
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.20f, 1.0f),
                       "%s memory warning: estimate is %.0f%% of available",
                       label,
                       ratio * 100.0);
  } else if (ratio >= 0.7) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f),
                       "%s memory caution: estimate is %.0f%% of available",
                       label,
                       ratio * 100.0);
  }
}

static void DrawPerformanceMemorySection(const SettingsMemoryView& memory,
                                         const RenderBackendCapabilities& backend,
                                         SettingsActionRequestState& req)
{
  if (!ImGui::CollapsingHeader("Performance / Memory")) {
    return;
  }

  ImGui::Text("Particles: %zu loaded, %zu renderable",
              memory.particleCount,
              memory.renderParticleCount);
  if (!backend.particles) {
    ImGui::TextDisabled("Current render backend does not draw particles.");
  }
  ImGui::SeparatorText("Rendering timings");
  ImGui::TextDisabled("Particle drawing");
  if (memory.timing.particleDrawActive) {
    if (memory.timing.particleDrawCacheHit) {
      if (memory.timing.particleDrawWallTimeKnown) {
        ImGui::Text("Draw: cache hit (last %.3f ms, %.1f Hz)",
                    memory.timing.particleDrawWallMs,
                    memory.timing.particleDrawRefreshHz);
      } else {
        ImGui::Text("Draw: cache hit");
      }
    } else if (memory.timing.particleDrawWallTimeKnown) {
      ImGui::Text("Draw: %.3f ms (%.1f Hz)",
                  memory.timing.particleDrawWallMs,
                  memory.timing.particleDrawRefreshHz);
    } else {
      ImGui::TextDisabled("Draw: waiting for timing");
    }
  } else {
    ImGui::TextDisabled("Draw timing: inactive");
  }
  ImGui::Spacing();
  ImGui::TextDisabled("Particle LOD update");
  if (memory.timing.particleGpuLodActive) {
    if (memory.timing.particleGpuLodCacheHit) {
      if (memory.timing.particleGpuLodWallTimeKnown) {
        ImGui::Text("GPU LOD: cache hit (last %.3f ms, %.1f Hz)",
                    memory.timing.particleGpuLodWallMs,
                    memory.timing.particleGpuLodRefreshHz);
      } else {
        ImGui::Text("GPU LOD: cache hit");
      }
    } else if (memory.timing.particleGpuLodUpdated &&
               memory.timing.particleGpuLodWallTimeKnown) {
      ImGui::Text("GPU LOD update: %.3f ms (%.1f Hz)",
                  memory.timing.particleGpuLodWallMs,
                  memory.timing.particleGpuLodRefreshHz);
    } else {
      ImGui::TextDisabled("GPU LOD: waiting for timing");
    }
    if (memory.timing.particleGpuLodDrawWallTimeKnown) {
      ImGui::Text("GPU LOD draw: %.3f ms (%.1f Hz)",
                  memory.timing.particleGpuLodDrawWallMs,
                  memory.timing.particleGpuLodDrawRefreshHz);
    }
    if (memory.timing.particleGpuLodIcbGenerationTimeKnown) {
      ImGui::Text("Range build: %.3f ms",
                  memory.timing.particleGpuLodIcbGenerationMs);
    }
    if (memory.timing.particleGpuLodIcbDrawTimeKnown) {
      ImGui::Text("Range draw encode: %.3f ms",
                  memory.timing.particleGpuLodIcbDrawMs);
    }
    if (memory.timing.particleGpuLodNormalDrawTimeKnown) {
      ImGui::Text("Normal fallback draw: %.3f ms",
                  memory.timing.particleGpuLodNormalDrawMs);
    }
    if (memory.timing.particleGpuLodStatsKnown) {
      ImGui::Text("Proxy nodes: %llu  Leaf ranges: %llu  Draw commands: %llu",
                  static_cast<unsigned long long>(
                    memory.timing.particleGpuLodAcceptedProxyNodes),
                  static_cast<unsigned long long>(
                    memory.timing.particleGpuLodAcceptedLeafRanges),
                  static_cast<unsigned long long>(
                    memory.timing.particleGpuLodGeneratedDrawCommands));
      ImGui::Text("Leaf particles in ranges: %llu",
                  static_cast<unsigned long long>(
                    memory.timing.particleGpuLodLeafParticleCount));
      ImGui::Text("Merged leaf ranges: %llu",
                  static_cast<unsigned long long>(
                    memory.timing.particleGpuLodMergedLeafRangeCount));
      if (ImGui::TreeNode("Advanced LOD debug")) {
        ImGui::Text("Visited nodes: %llu",
                    static_cast<unsigned long long>(
                      memory.timing.particleGpuLodVisitedNodes));
        ImGui::Text("Expanded: %llu  Children: %llu  Frustum culled: %llu",
                    static_cast<unsigned long long>(
                      memory.timing.particleGpuLodExpandedNodes),
                    static_cast<unsigned long long>(
                      memory.timing.particleGpuLodAppendedChildren),
                    static_cast<unsigned long long>(
                      memory.timing.particleGpuLodFrustumCulledNodes));
        ImGui::Text("Max leaf count: %u",
                    memory.timing.particleGpuLodMaxLeafCount);
        if (memory.timing.particleGpuLodLevelCount > 0 &&
            ImGui::TreeNode("Level projected size")) {
          ImGui::TextDisabled("level visited proxy leaf expanded min/avg/max px");
          for (std::uint32_t level = 0;
               level < memory.timing.particleGpuLodLevelCount;
               ++level) {
            const auto& stats = memory.timing.particleGpuLodLevels[level];
            if (stats.visited == 0) {
              continue;
            }
            ImGui::Text("%2u  %7u  %5u  %5u  %6u  %.2f / %.2f / %.2f",
                        level,
                        stats.visited,
                        stats.proxy,
                        stats.leaf,
                        stats.expanded,
                        stats.minProjectedPx,
                        stats.avgProjectedPx,
                        stats.maxProjectedPx);
          }
          ImGui::TreePop();
        }
        ImGui::TreePop();
      }
    }
  } else {
    ImGui::TextDisabled("GPU LOD timing: inactive");
  }
  ImGui::Spacing();
  ImGui::TextDisabled("Volume rendering");
  if (memory.timing.volumeCacheUsed) {
    const char* cacheState = memory.timing.volumeCacheUpdated
      ? "updated"
      : (memory.timing.volumeCacheHit ? "hit" : "idle");
    if (memory.timing.volumeGpuTimeKnown) {
      ImGui::Text("Last volume ray pass GPU: %.3f ms (%s, %.2fx cache)",
                  memory.timing.volumeGpuMs,
                  cacheState,
                  memory.timing.volumeCacheScale);
      if (memory.timing.volumeWallLatencyKnown) {
        ImGui::Text("Last volume completion latency: %.3f ms CPU clock",
                    memory.timing.volumeWallLatencyMs);
      }
    } else {
      if (memory.timing.volumeWallLatencyKnown) {
        ImGui::Text("Last volume completion latency: %.3f ms CPU clock (%s, %.2fx cache)",
                    memory.timing.volumeWallLatencyMs,
                    cacheState,
                    memory.timing.volumeCacheScale);
      } else {
        ImGui::Text("Volume ray pass: waiting for timing (%s, %.2fx cache)",
                    cacheState,
                    memory.timing.volumeCacheScale);
      }
    }
    ImGui::TextDisabled("Latency is CPU-observed time until GPU completion is visible.");
  } else {
    ImGui::TextDisabled("Volume ray pass timing: inactive");
  }
  ImGui::SeparatorText("Scene size");
  ImGui::Text("Particle LOD proxy: %zu points",
              memory.particleLodProxyCount);
  ImGui::Text("Particle LOD nodes: %zu", memory.particleLodNodeCount);
#ifdef VOLUME_RENDERING
  ImGui::Text("Volume nodes: %zu", memory.volumeNodeCount);
#endif

  ImGui::SeparatorText("Estimated memory used by this app");
  const size_t cpuEstimate =
    memory.cpuParticleBytes +
    memory.cpuRenderSceneBytes +
    memory.cpuParticleLodTreeBytes;
  const size_t gpuEstimate =
    memory.gpuParticleBufferBytes +
    memory.gpuParticleCacheBytes +
    memory.gpuVolumeTreeBytes +
    memory.gpuVolumeCacheBytes;
  ImGui::Text("CPU particles: %s", FormatBytes(memory.cpuParticleBytes));
  ImGui::Text("CPU render scene: %s", FormatBytes(memory.cpuRenderSceneBytes));
  ImGui::Text("CPU particle LOD tree: %s",
              FormatBytes(memory.cpuParticleLodTreeBytes));
  ImGui::Text("CPU estimated total: %s", FormatBytes(cpuEstimate));
  if (memory.processMemoryKnown) {
    ImGui::Text("Process resident memory: %s",
                FormatBytes(memory.processMemoryBytes));
  }
  DrawMemoryPressureWarning("CPU",
                            cpuEstimate,
                            memory.systemAvailableKnown,
                            memory.systemAvailableBytes);
  if (backend.particles) {
    ImGui::Text("GPU particle buffer: %s",
                FormatBytes(memory.gpuParticleBufferBytes));
  }
  if (backend.particleFrameCache) {
    ImGui::Text("GPU particle frame cache: %s",
                FormatBytes(memory.gpuParticleCacheBytes));
  }
#ifdef VOLUME_RENDERING
  if (backend.volumeRendering) {
    ImGui::Text("GPU volume tree buffers: %s",
                FormatBytes(memory.gpuVolumeTreeBytes));
  }
  if (backend.volumeFrameCache) {
    ImGui::Text("GPU volume frame cache: %s",
                FormatBytes(memory.gpuVolumeCacheBytes));
  }
#endif
  ImGui::Text("GPU estimated total: %s", FormatBytes(gpuEstimate));
  if (!memory.gpuDeviceName.empty()) {
    ImGui::Text("GPU device: %s", memory.gpuDeviceName.c_str());
  }
  if (memory.gpuAllocatedKnown) {
    ImGui::Text("GPU driver allocated: %s",
                FormatBytes(memory.gpuAllocatedBytes));
  }
  if (memory.gpuBudgetKnown) {
    ImGui::Text("GPU budget / recommended working set: %s",
                FormatBytes(memory.gpuBudgetBytes));
  }
  if (memory.gpuAllocatedKnown &&
      memory.gpuBudgetKnown &&
      memory.gpuBudgetBytes > 0) {
    const double usage =
      100.0 * static_cast<double>(memory.gpuAllocatedBytes) /
      static_cast<double>(memory.gpuBudgetBytes);
    ImGui::Text("GPU driver usage: %.1f%%", usage);
  }
  DrawMemoryPressureWarning("GPU",
                            gpuEstimate,
                            memory.gpuAvailableKnown,
                            memory.gpuAvailableBytes);

  ImGui::SeparatorText("Interaction / caches");
  auto& scheduling = req.renderDraft.scheduling;
  if (!req.particleLodTreeDraftInitialized) {
    req.particleLodMinNodeParticlesDraft =
      scheduling.particleLod.minNodeParticles;
    req.particleLodMaxDepthDraft = scheduling.particleLod.maxDepth;
    req.particleLodTreeDraftInitialized = true;
  }
  bool dirty = false;
  dirty |= ImGui::Checkbox("Responsive interaction",
                           &scheduling.responsiveInteraction);
  dirty |= ImGui::InputFloat("Interaction settle delay [s]",
                             &scheduling.settleDelaySeconds,
                             0.01f,
                             0.05f,
                             "%.3f");
  if (scheduling.settleDelaySeconds < 0.0f) {
    scheduling.settleDelaySeconds = 0.0f;
    dirty = true;
  }
#ifdef VOLUME_RENDERING
  dirty |= ImGui::Checkbox("Hide volume while interacting",
                           &scheduling.skipVolumeWhileInteracting);
#endif
  if (!backend.particleFrameCache) {
    ImGui::BeginDisabled();
  }
  dirty |= ImGui::Checkbox("Cache unchanged particle frames",
                           &scheduling.cacheParticleFrames);
  if (!backend.particleFrameCache) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("unsupported by backend");
  }
#ifdef VOLUME_RENDERING
  if (!backend.volumeFrameCache) {
    ImGui::BeginDisabled();
  }
  dirty |= ImGui::Checkbox("Cache unchanged volume frames",
                           &scheduling.cacheVolumeFrames);
  if (backend.volumeFrameCache && scheduling.cacheVolumeFrames) {
    dirty |= ImGui::SliderFloat("Volume cache resolution",
                                &scheduling.volumeFrameCacheScale,
                                0.25f,
                                1.0f,
                                "%.2fx");
    scheduling.volumeFrameCacheScale =
      std::clamp(scheduling.volumeFrameCacheScale, 0.25f, 1.0f);
    ImGui::TextDisabled("Lower values redraw volume faster, with softer detail.");
  }
  if (!backend.volumeFrameCache) {
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("unsupported by backend");
  }
#endif

  ImGui::SeparatorText("Particle LOD");
  if (!backend.particleLod) {
    ImGui::BeginDisabled();
  }
  int particleLodMode = static_cast<int>(scheduling.particleLod.mode);
  const char* lodLabels[] = {"Off", "While interacting", "Always"};
  if (ImGui::Combo("Particle LOD mode",
                   &particleLodMode,
                   lodLabels,
                   IM_ARRAYSIZE(lodLabels))) {
    scheduling.particleLod.mode =
      static_cast<ParticleLodMode>(particleLodMode);
    dirty = true;
  }
  if (backend.particleGpuLod) {
    ImGui::TextDisabled("GPU LOD uses projected node size.");
    dirty |= ImGui::InputFloat("LOD screen threshold [px]",
                               &scheduling.particleLod.screenPixelThreshold,
                               0.25f,
                               1.0f,
                               "%.2f");
    if (scheduling.particleLod.screenPixelThreshold < 0.05f) {
      scheduling.particleLod.screenPixelThreshold = 0.05f;
      dirty = true;
    }
    dirty |= ImGui::InputFloat("LOD update rate [Hz]",
                               &scheduling.particleLod.proxyUpdateRateHz,
                               1.0f,
                               5.0f,
                               "%.1f");
    if (scheduling.particleLod.proxyUpdateRateHz < 0.0f) {
      scheduling.particleLod.proxyUpdateRateHz = 0.0f;
      dirty = true;
    }
    ImGui::TextDisabled("Larger values draw fewer proxy particles.");
    ImGui::TextDisabled("Update rate limits GPU LOD refresh while moving.");
  } else {
    dirty |= ImGui::Checkbox("Auto LOD on software renderer",
                             &scheduling.autoParticleLodOnSoftwareRenderer);
    dirty |= ImGui::InputFloat("LOD proxy fraction",
                               &scheduling.particleLod.proxyFraction,
                               0.05f,
                               0.1f,
                               "%.2f");
    if (scheduling.particleLod.proxyFraction < 0.01f) {
      scheduling.particleLod.proxyFraction = 0.01f;
      dirty = true;
    }
    if (scheduling.particleLod.proxyFraction > 1.0f) {
      scheduling.particleLod.proxyFraction = 1.0f;
      dirty = true;
    }
    dirty |= ImGui::InputFloat("LOD theta",
                               &scheduling.particleLod.theta,
                               0.01f,
                               0.05f,
                               "%.3f");
    if (scheduling.particleLod.theta < 0.01f) {
      scheduling.particleLod.theta = 0.01f;
      dirty = true;
    }
    dirty |= ImGui::InputFloat("LOD focus update distance",
                               &scheduling.particleLod.focusUpdateDistance,
                               0.01f,
                               0.05f,
                               "%.3f");
    if (scheduling.particleLod.focusUpdateDistance < 0.0f) {
      scheduling.particleLod.focusUpdateDistance = 0.0f;
      dirty = true;
    }
    dirty |= ImGui::InputFloat("LOD proxy update rate [Hz]",
                               &scheduling.particleLod.proxyUpdateRateHz,
                               1.0f,
                               5.0f,
                               "%.1f");
    if (scheduling.particleLod.proxyUpdateRateHz < 0.0f) {
      scheduling.particleLod.proxyUpdateRateHz = 0.0f;
      dirty = true;
    }
  }

  int minNodeParticles =
    static_cast<int>(req.particleLodMinNodeParticlesDraft);
  if (ImGui::InputInt("LOD min particles per node",
                      &minNodeParticles,
                      8,
                      64)) {
    if (minNodeParticles < 1) minNodeParticles = 1;
    req.particleLodMinNodeParticlesDraft =
      static_cast<std::uint32_t>(minNodeParticles);
  }
  int maxDepth = static_cast<int>(req.particleLodMaxDepthDraft);
  if (ImGui::InputInt("LOD max tree depth", &maxDepth, 1, 4)) {
    if (maxDepth < 1) maxDepth = 1;
    if (maxDepth > 30) maxDepth = 30;
    req.particleLodMaxDepthDraft = static_cast<std::uint32_t>(maxDepth);
  }
  if (ImGui::Button("Apply LOD tree settings")) {
    scheduling.particleLod.minNodeParticles =
      req.particleLodMinNodeParticlesDraft;
    scheduling.particleLod.maxDepth = req.particleLodMaxDepthDraft;
    dirty = true;
  }
  ImGui::TextDisabled("Tree settings rebuild the tree only when Apply is pressed.");
  if (backend.particleGpuLod) {
    ImGui::TextDisabled("Pixel threshold reuses the tree and updates GPU LOD.");
  } else {
    ImGui::TextDisabled("Theta/proxy settings reuse the tree and rebuild only the proxy.");
  }
  if (!backend.particleLod) {
    ImGui::EndDisabled();
    ImGui::TextDisabled("Particle LOD is unsupported by the current backend.");
  }

  if (dirty) {
    req.renderDraftDirty = true;
    req.applyRenderRequested = true;
  }
}

static void DrawParticleTypeSettingsSection(const QuantityState& quantity,
					    SettingsActionRequestState& req) {
  if (!ImGui::CollapsingHeader("Particle Type Settings"))
    return;

  unsigned int visibleMask = 0;
  for (int type = 0; type < 6; ++type) {
    if (!req.particleVisualDraft.types[type].hideParticles) {
      visibleMask |= static_cast<unsigned int>(1u << type);
    }
  }
  ImGui::TextDisabled("Visible particle types");
  if (DrawFramedTypeMaskToggleButtons(visibleMask, "particle_type_visible")) {
    for (int type = 0; type < 6; ++type) {
      const unsigned int bit = static_cast<unsigned int>(1u << type);
      req.particleVisualDraft.types[type].hideParticles =
        (visibleMask & bit) == 0;
    }
    req.particleVisualDraftDirty = true;
    req.applyParticleVisualRequested = true;
    req.particleRenderDirtyRequested = true;
  }
  ImGui::Spacing();

  for (int i = 0; i < 6; i++) {
    std::string header = "Type " + std::to_string(i);
    if (ImGui::TreeNode(header.c_str())) {
      auto& cfg = req.particleVisualDraft.types[i];
      bool visualChanged = false;
      const ColormapDef* colormaps = AvailableColormaps();
      const int colormapCount = AvailableColormapCount();
				
      std::string comboLabel = "Colormap##" + std::to_string(i);
      const char* preview = colormaps[cfg.colormapIndex].name;
      if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
	for (int k = 0; k < colormapCount; ++k) {
	  bool selected = (cfg.colormapIndex == k);
	  if (ImGui::Selectable(colormaps[k].name, selected)) {
	    cfg.colormapIndex = k;
	    visualChanged = true;
	  }
	  if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
				
      visualChanged |= ImGui::Checkbox("Periodic Color Bar", &cfg.periodicColorBar);
				
      std::string sliderLabel = "Point Size##" + std::to_string(i);
      visualChanged |= ImGui::SliderFloat(sliderLabel.c_str(), &cfg.pointSize, 1.0f, 100.0f);
      std::string minLabel = "Value Min##" + std::to_string(i);
      visualChanged |= ImGui::InputFloat(minLabel.c_str(), &cfg.colorMin, 0.01f, 0.1f, "%.3f");
      std::string maxLabel = "Value Max##" + std::to_string(i);
      visualChanged |= ImGui::InputFloat(maxLabel.c_str(), &cfg.colorMax, 0.01f, 0.1f, "%.3f");
      std::string logLabel = "Use Log Scale##" + std::to_string(i);
      visualChanged |= ImGui::Checkbox(logLabel.c_str(), &cfg.useLogScale);
				
      QuantityId& sel = cfg.selectedQuantity;
      const int typeQuantityCount =
        quantity.catalog.nUIQByType[static_cast<size_t>(i)];
      auto quantityAvailableForType = [&](QuantityId q) {
        for (int qi = 0; qi < typeQuantityCount; ++qi) {
          if (quantity.catalog.uiQByType[static_cast<size_t>(i)][qi] == q) {
            return true;
          }
        }
        return false;
      };
      if (typeQuantityCount > 0 && !quantityAvailableForType(sel)) {
        sel = quantity.catalog.uiQByType[static_cast<size_t>(i)][0];
        visualChanged = true;
      }

      std::string quantityLabel = "Quantity##ptype_" + std::to_string(i);
      if (typeQuantityCount <= 0) {
        ImGui::TextDisabled("No readable fields for this particle type");
      } else if (ImGui::BeginCombo(quantityLabel.c_str(),
                                   QuantityDisplayLabel(quantity, sel))) {
        for (int q = 0; q < typeQuantityCount; ++q) {
          QuantityId cand =
            quantity.catalog.uiQByType[static_cast<size_t>(i)][q];
          bool is_selected = (cand == sel);
          if (ImGui::Selectable(QuantityDisplayLabel(quantity, cand),
                                is_selected)) {
            sel = cand;
            visualChanged = true;
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
				
      const int qidx = static_cast<int>(sel);
      ImGui::Text("Current particle %s range: [%g, %g]",
		  QuantityDisplayLabel(quantity, sel),
		  quantity.range.valueMin[qidx][i],
		  quantity.range.valueMax[qidx][i]);
				
      if (visualChanged) {
        req.particleVisualDraftDirty = true;
        req.applyParticleVisualRequested = true;
	req.particleRenderDirtyRequested = true;
      }
				
      ImGui::TreePop();
    }
  }    
}

static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      WindowCommandQueue& windowCommands){
  if(!ImGui::CollapsingHeader("File Navigation"))
    return;

  auto& nav = rt.navigation;
  auto& input = rt.input;
  
  ImGui::InputText("Folder", input.folderPath, IM_ARRAYSIZE(input.folderPath));
  ImGui::InputText("File Format", input.fileFormat, IM_ARRAYSIZE(input.fileFormat));
  ImGui::InputInt("initialIndex", &nav.initialIndex);
		
  RefreshSnapshotFilePath(rt);
#ifdef HAVE_HDF5
  if (IsHDF5SnapshotPath(input.filePath)) {
    input.useHDF5 = true;
    if (format.readFormat != FileFormat::HDF5) {
      format.readFormat = FileFormat::HDF5;
    }
  }
#endif
		
  if (ImGui::Button("Browse Files")) {
#ifndef NONATIVEFILEDIALOG
    nfdu8char_t* outPath = nullptr;

    nfdopendialogu8args_t args = {};
    //args.filterList  = filters;
    //args.filterCount = 1;
    args.filterList  = nullptr; 
    args.filterCount = 0;    
    args.defaultPath = input.folderPath[0] ? input.folderPath : nullptr;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY) {
      ApplySelectedSnapshotPath(rt, outPath);
#ifdef HAVE_HDF5
      if (IsHDF5SnapshotPath(outPath)) {
        format.readFormat = FileFormat::HDF5;
      }
#endif
      NFD_FreePathU8(outPath);
    }
    else if (result == NFD_CANCEL) {
    }
    else {
      std::cerr << "Error: " << NFD_GetError() << std::endl;
    }
#else
    IGFD::FileDialogConfig config;
    // Set the initial directory via the "path" member.
    //config.path = src.filePath;
    config.path = input.folderPath;
    // Set an initial filename if needed; empty waits for user input.
    config.fileName = "output"; 
    // Leave other options such as selectable file count at their defaults.
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "**", config);
#endif
  }
		
#ifdef NONATIVEFILEDIALOG
  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
    {
      if (ImGuiFileDialog::Instance()->IsOk())
	{
	  std::string selectedFile = ImGuiFileDialog::Instance()->GetFilePathName();
	  ApplySelectedSnapshotPath(rt, selectedFile.c_str());
#ifdef HAVE_HDF5
          if (IsHDF5SnapshotPath(selectedFile.c_str())) {
            format.readFormat = FileFormat::HDF5;
          }
#endif
	}
      else
	{
	  ImGui::Text("File Dialog Cancelled");
	}
      ImGuiFileDialog::Instance()->Close();
    }
#endif
  
  RecomputeCurrentFileIndex(rt);
  ImGui::Text("File: %s", input.filePath);
  ImGui::Text("Current File: %d", nav.currentFileIndex);
		
  ImGui::BeginDisabled(isLoading);

  if (rt.tempSkipStep <= 0) {
    rt.tempSkipStep = nav.skipStep;
  }

  if (ImGui::InputInt("Skip Step", &rt.tempSkipStep, 1, 100)) {
    rt.request.applySkipStepRequested = true;
  }

  if (ImGui::InputInt("Select File Index", &nav.currentStep, 1, 10)) {
    rt.request.loadSelectedSnapshotRequested = true;
  }

  if (ImGui::Button("Previous File") && nav.currentStep > 0) {
    rt.request.loadPreviousRequested = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("Next File")) {
    rt.request.loadNextRequested = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("Reload")) {
    rt.request.reloadRequested = true;
  }

  if (ImGui::InputInt("Batch Size", &nav.batchSize)) {
    rt.request.loadBatchRequested = true;
  }

  ImGui::EndDisabled();

  if (isLoading) {
    ImGui::Text("Loading...");
  }
  if (rt.lastLoadError[0] != '\0') {
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                       "Load failed: %s",
                       rt.lastLoadError);
  }

  static const char* FileFormatNames[] = {
    "Auto", "HDF5", "Binary", "Gadget", "Framed"
  };
  static_assert(static_cast<int>(FileFormat::_Count) == IM_ARRAYSIZE(FileFormatNames),
                "FileFormatNames needs to match FileFormat::_Count");

  int fmtIdx = static_cast<int>(format.readFormat);
  float formatComboWidth = 0.0f;
  for (const char* name : FileFormatNames) {
    formatComboWidth = std::max(formatComboWidth, ImGui::CalcTextSize(name).x);
  }
  formatComboWidth += ImGui::GetFrameHeight() +
                      2.0f * ImGui::GetStyle().FramePadding.x;

  ImGui::TextUnformatted("Data format");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(formatComboWidth);
  if (ImGui::Combo("##read_data_format", &fmtIdx, FileFormatNames, IM_ARRAYSIZE(FileFormatNames))) {
    format.readFormat = static_cast<FileFormat>(fmtIdx);
  }
  ImGui::SameLine();
  if (ImGui::Button("Edit Data Format")) {
#ifdef HAVE_HDF5
    const bool useHdf5FormatDialog =
      format.readFormat == FileFormat::HDF5 ||
      (format.readFormat == FileFormat::Auto && input.useHDF5);
    if (useHdf5FormatDialog) {
      rt.request.openHDF5FormatDialogRequested = true;
    } else
#endif
    {
      rt.request.openFormatDialogRequested = true;
    }
  }

  if (ImGui::TreeNode("Advanced settings")) {
    if (ImGui::Button("Mask Settings...")) {
      windowCommands.open(WindowId::Mask);
    }

    if (ImGui::Button("Generate test data")) {
      rt.request.generateTestDataRequested = true;
    }
    ImGui::TreePop();
  }
}

static void SyncSnapshotExtractUnitDefaults(SnapshotExtractUiState& state,
                                            const FileNavigationRuntimeState& fileNav,
                                            const UnitSystem& units)
{
  const std::string sourcePath = fileNav.input.filePath;
  if (state.unitDefaultsSourcePath == sourcePath) return;
  state.unitDefaultsSourcePath = sourcePath;
  state.targetUnitLengthCm = units.length_cm;
  state.targetUnitMassG = units.mass_g;
  state.targetUnitVelocityCmPerS = units.velocity_cm_per_s;
  state.targetHubbleParam = units.hubble;
  state.targetScaleFactor =
    std::max(1.0e-30, fileNav.current.loadedScaleFactor);
}

static double SnapshotExtractPreviewLengthScale(const SnapshotExtractUiState& state,
                                                const UnitSystem& units)
{
  const double sourceLength = std::max(1.0e-30, units.length_cm);
  const double targetLength = std::max(1.0e-30, state.targetUnitLengthCm);
  double scale = sourceLength / targetLength;
  const double a = std::max(1.0e-30, state.targetScaleFactor);
  const auto mode = static_cast<SnapshotExtractComovingMode>(state.comovingMode);
  if (mode == SnapshotExtractComovingMode::ComovingToPhysical) {
    scale *= a;
  } else if (mode == SnapshotExtractComovingMode::PhysicalToComoving) {
    scale /= a;
  }
  return scale;
}

static double SnapshotExtractBackgroundMeanVolume(const SnapshotExtractUiState& state,
                                                  const UnitSystem& units)
{
  const int n = std::clamp(state.backgroundCellsPerAxis, 1, 512);
  const double lengthScale = SnapshotExtractPreviewLengthScale(state, units);
  double dx = 0.0;
  double dy = 0.0;
  double dz = 0.0;
  if (state.regionKind == static_cast<int>(SnapshotExtractRegionKind::Sphere)) {
    const double side = 2.0 * std::max(0.0, state.radius) * lengthScale;
    dx = dy = dz = side / static_cast<double>(n);
  } else {
    dx = 2.0 * std::max(0.0, state.halfSize[0]) * lengthScale /
         static_cast<double>(n);
    dy = 2.0 * std::max(0.0, state.halfSize[1]) * lengthScale /
         static_cast<double>(n);
    dz = 2.0 * std::max(0.0, state.halfSize[2]) * lengthScale /
         static_cast<double>(n);
  }
  if (!(dx > 0.0 && dy > 0.0 && dz > 0.0)) return 0.0;
  return dx * dy * dz;
}

static void DrawSnapshotExtractSection(FileNavigationRuntimeState& fileNav,
                                       SnapshotFormatState& format,
                                       const SettingsCameraView& camera,
                                       const UnitSystem& units,
                                       SettingsActionRequestState& request)
{
  auto& state = request.snapshotExtract;
  SyncSnapshotExtractUnitDefaults(state, fileNav, units);
  ImGui::SeparatorText("Extract Snapshot");
  ImGui::TextDisabled("Raw HDF5 extract: rereads the source file and writes selected rows.");
  ImGui::TextDisabled("Region selection is evaluated in source code coordinates before output unit conversion.");

  bool previewChanged = false;
  const char* regionKinds[] = {"box", "sphere"};
  previewChanged |=
    ImGui::Combo("region shape##snapshot_extract",
                 &state.regionKind,
                 regionKinds,
                 IM_ARRAYSIZE(regionKinds));
  previewChanged |=
    ImGui::Checkbox("show extract region##snapshot_extract", &state.showRegion);

  if (ImGui::SmallButton("camera center##snapshot_extract")) {
    for (int axis = 0; axis < 3; ++axis) {
      state.center[axis] = camera.originalTarget[axis];
    }
    previewChanged = true;
  }
  previewChanged |=
    ImGui::InputScalarN("center (source code units)##snapshot_extract",
                        ImGuiDataType_Double,
                        state.center,
                        3,
                        nullptr,
                        nullptr,
                        "%.12g");
  if (state.regionKind == static_cast<int>(SnapshotExtractRegionKind::Sphere)) {
    previewChanged |=
      ImGui::InputDouble("radius (source code units)##snapshot_extract",
                         &state.radius,
                         0.0,
                         0.0,
                         "%.12g");
    state.radius = std::max(0.0, state.radius);
  } else {
    double boxSize[3] = {
      2.0 * std::max(0.0, state.halfSize[0]),
      2.0 * std::max(0.0, state.halfSize[1]),
      2.0 * std::max(0.0, state.halfSize[2])
    };
    if (ImGui::InputScalarN("box size (source code units)##snapshot_extract",
                            ImGuiDataType_Double,
                            boxSize,
                            3,
                            nullptr,
                            nullptr,
                            "%.12g")) {
      for (int axis = 0; axis < 3; ++axis) {
        state.halfSize[axis] = 0.5 * std::max(0.0, boxSize[axis]);
      }
      previewChanged = true;
    }
  }
  if (previewChanged) {
    request.snapshotExtractPreviewRequested = true;
  }

  ImGui::InputText("output HDF5##snapshot_extract",
                   state.outputPath,
                   IM_ARRAYSIZE(state.outputPath));
  ImGui::Checkbox("use output format override##snapshot_extract",
                  &format.outputFormat.enabled);
  ImGui::SameLine();
  if (ImGui::Button("Edit output format##snapshot_extract")) {
    fileNav.request.openOutputFormatDialogRequested = true;
  }
  ImGui::Checkbox("copy Header##snapshot_extract", &state.copyHeader);
  ImGui::SameLine();
  ImGui::Checkbox("copy Parameters##snapshot_extract", &state.copyParameters);

  if (ImGui::TreeNodeEx("Particle IDs##snapshot_extract",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("offset ParticleIDs##snapshot_extract",
                    &state.offsetParticleIds);
    ImGui::BeginDisabled(!state.offsetParticleIds);
    std::uint64_t idOffset = state.particleIdOffset;
    if (ImGui::InputScalar("ID offset##snapshot_extract",
                           ImGuiDataType_U64,
                           &idOffset,
                           nullptr,
                           nullptr,
                           "%llu")) {
      state.particleIdOffset = idOffset;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
      "Output ParticleIDs are written as input ID + offset. Use +1 for Gadget IDs that may contain 0.");
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Background grid##snapshot_extract",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("add background grid##snapshot_extract", &state.addBackgroundGrid);
    ImGui::BeginDisabled(!state.addBackgroundGrid);
    ImGui::PushItemWidth(90.0f);
    ImGui::InputInt("N##snapshot_extract_bg",
                    &state.backgroundCellsPerAxis,
                    0,
                    0);
    state.backgroundCellsPerAxis =
      std::clamp(state.backgroundCellsPerAxis, 1, 512);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextUnformatted("for N^3 cells");
    ImGui::SameLine();
    ImGui::PushItemWidth(150.0f);
    ImGui::InputDouble("density##snapshot_extract_bg",
                       &state.backgroundDensity,
                       0.0,
                       0.0,
                       "%.8g");
    ImGui::PopItemWidth();
    ImGui::TextDisabled(
      "IDs are assigned as max selected ParticleID + 1, +2, ...");
    ImGui::TextDisabled(
      "Velocity, B, metallicity, J21, H2, and thermal fields are copied from the nearest selected gas cell when available.");
    ImGui::TextDisabled("Density is manual and interpreted in the output code units above.");
    const int n = std::clamp(state.backgroundCellsPerAxis, 1, 512);
    const std::size_t candidateCount =
      static_cast<std::size_t>(n) * static_cast<std::size_t>(n) *
      static_cast<std::size_t>(n);
    const double meanVolume =
      SnapshotExtractBackgroundMeanVolume(state, units);
    const double referenceMass = meanVolume * state.backgroundDensity;
    ImGui::Text("candidate particles=%zu", candidateCount);
    ImGui::Text("Suggested value for MeanVolume=%.8g", meanVolume);
    ImGui::Text("Suggested value for ReferenceGasPartMass=%.8g",
                referenceMass);
    ImGui::TextDisabled(
      "Actual added particles are printed after excluding cells within 0.5 grid spacing of selected gas.");
    ImGui::EndDisabled();
    ImGui::TreePop();
  }

  ImGui::Text("source: %s", fileNav.input.filePath);
  if (ImGui::Button("Unit conversion...##snapshot_extract")) {
    state.showUnitWindow = true;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("output units are always written");

  if (state.showUnitWindow) {
    ImGui::Begin("Snapshot extract unit conversion",
                 &state.showUnitWindow,
                 ImGuiWindowFlags_AlwaysAutoResize);
    const char* modes[] = {
      "preserve comoving flag",
      "comoving -> physical",
      "physical -> comoving"
    };
    const int previousMode = state.comovingMode;
    if (ImGui::Combo("comoving conversion##snapshot_extract_units",
                     &state.comovingMode,
                     modes,
                     IM_ARRAYSIZE(modes)) &&
        state.comovingMode != static_cast<int>(SnapshotExtractComovingMode::Preserve)) {
      state.targetScaleFactor =
        std::max(1.0e-30, fileNav.current.loadedScaleFactor);
    }
    if (previousMode == static_cast<int>(SnapshotExtractComovingMode::Preserve) &&
        state.comovingMode != previousMode) {
      state.targetScaleFactor =
        std::max(1.0e-30, fileNav.current.loadedScaleFactor);
    }
    ImGui::InputDouble("scale factor a##snapshot_extract_units",
                       &state.targetScaleFactor,
                       0.0,
                       0.0,
                       "%.8g");
    state.targetScaleFactor = std::max(1.0e-30, state.targetScaleFactor);
    ImGui::SeparatorText("Target code units");
    ImGui::TextDisabled("Source units are the loaded file Parameters; if absent, current Units are used.");
    ImGui::InputDouble("UnitLength_in_cm##snapshot_extract_units",
                       &state.targetUnitLengthCm,
                       0.0,
                       0.0,
                       "%.8g");
    ImGui::SameLine();
    if (ImGui::SmallButton("Mpc##snapshot_extract_units")) {
      state.targetUnitLengthCm = 3.0856775814913673e24;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("kpc##snapshot_extract_units")) {
      state.targetUnitLengthCm = 3.0856775814913673e21;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("pc##snapshot_extract_units")) {
      state.targetUnitLengthCm = 3.0856775814913673e18;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("au##snapshot_extract_units")) {
      state.targetUnitLengthCm = 1.495978707e13;
    }
    ImGui::InputDouble("UnitMass_in_g##snapshot_extract_units",
                       &state.targetUnitMassG,
                       0.0,
                       0.0,
                       "%.8g");
    ImGui::SameLine();
    if (ImGui::SmallButton("1e10 Msun##snapshot_extract_units")) {
      state.targetUnitMassG = 1.98847e43;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Msun##snapshot_extract_units")) {
      state.targetUnitMassG = 1.98847e33;
    }
    ImGui::InputDouble("UnitVelocity_in_cm_per_s##snapshot_extract_units",
                       &state.targetUnitVelocityCmPerS,
                       0.0,
                       0.0,
                       "%.8g");
    ImGui::InputDouble("HubbleParam##snapshot_extract_units",
                       &state.targetHubbleParam,
                       0.0,
                       0.0,
                       "%.8g");
    state.targetUnitLengthCm = std::max(1.0e-30, state.targetUnitLengthCm);
    state.targetUnitMassG = std::max(1.0e-30, state.targetUnitMassG);
    state.targetUnitVelocityCmPerS =
      std::max(1.0e-30, state.targetUnitVelocityCmPerS);
    state.targetHubbleParam = std::max(1.0e-30, state.targetHubbleParam);
    if (ImGui::SmallButton("Use current Units##snapshot_extract_units")) {
      state.unitDefaultsSourcePath.clear();
      SyncSnapshotExtractUnitDefaults(state, fileNav, units);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Use loaded a##snapshot_extract_units")) {
      state.targetScaleFactor =
        std::max(1.0e-30, fileNav.current.loadedScaleFactor);
    }
    ImGui::TextWrapped("Coordinates, BoxSize, Hsml, Volume, Masses, Velocities, "
                       "Density, InternalEnergy, and MagneticField are converted "
                       "from the source Units to the target Units above.");
    ImGui::End();
  }

  if (ImGui::Button("Extract HDF5 snapshot##snapshot_extract")) {
    SnapshotExtractJob job;
    job.inputPath = fileNav.input.filePath;
    job.outputPath = state.outputPath;
    job.region.kind =
      state.regionKind == static_cast<int>(SnapshotExtractRegionKind::Sphere)
        ? SnapshotExtractRegionKind::Sphere
        : SnapshotExtractRegionKind::Box;
    for (int axis = 0; axis < 3; ++axis) {
      job.region.center[axis] = state.center[axis];
      job.region.halfSize[axis] = state.halfSize[axis];
    }
    job.region.radius = state.radius;
    job.copyHeader = state.copyHeader;
    job.copyParameters = state.copyParameters;
    job.unitConversion.enabled = true;
    job.unitConversion.sourceUnitLengthCm = units.length_cm;
    job.unitConversion.sourceUnitMassG = units.mass_g;
    job.unitConversion.sourceUnitVelocityCmPerS = units.velocity_cm_per_s;
    job.unitConversion.sourceHubbleParam = units.hubble;
    job.unitConversion.sourceScaleFactor = fileNav.current.loadedScaleFactor;
    job.unitConversion.comovingMode =
      static_cast<SnapshotExtractComovingMode>(state.comovingMode);
    job.unitConversion.unitLengthCm = state.targetUnitLengthCm;
    job.unitConversion.unitMassG = state.targetUnitMassG;
    job.unitConversion.unitVelocityCmPerS = state.targetUnitVelocityCmPerS;
    job.unitConversion.hubbleParam = state.targetHubbleParam;
    job.unitConversion.scaleFactor = state.targetScaleFactor;
    job.backgroundGrid.enabled = state.addBackgroundGrid;
    job.backgroundGrid.cellsPerAxis = state.backgroundCellsPerAxis;
    job.backgroundGrid.density = state.backgroundDensity;
    job.particleIdTransform.offsetEnabled = state.offsetParticleIds;
    job.particleIdTransform.offset = state.particleIdOffset;
    job.fields = ExtractFieldsForCurrentFormat(fileNav, format);
    job.outputFormat = format.outputFormat;
    if (job.outputFormat.fields.empty()) {
      job.outputFormat.fields = MakeDefaultSnapshotOutputFields();
    }
    request.snapshotExtractJob = std::move(job);
    request.snapshotExtractRequested = true;
    request.snapshotExtractMessage = "Extract requested...";
  }

  if (!request.snapshotExtractMessage.empty()) {
    ImGui::TextWrapped("%s", request.snapshotExtractMessage.c_str());
  }
}



static void DrawNormalizationSection(NormalizationContext& normalization,
				     SettingsActionRequestState& req) {
  ImGui::InputFloat("Desired Maximum", &normalization.desiredMax, 0.f, 0.f, "%g");
  if (ImGui::Button("Normalize Positions")) {
    req.normalizeRequested = true;
  }

  ImGui::Text("Max coordinate: %.3g", normalization.originalMax);
  ImGui::Text("Max coordinate is normalized to: %.3f", normalization.desiredMax);
}

static void DrawSinkIdSection(const SettingsCameraView& camera,
	                              SettingsActionRequestState& req)
{
  auto& labels = req.renderDraft.particleLabels;
  bool changed = false;

  float queryRadiusOriginal =
    labels.queryRadius * camera.renderToWorldScale;
  if (ImGui::InputFloat("radius", &queryRadiusOriginal, 0.f, 0.f, "%g")) {
    const float worldToRender =
      (camera.renderToWorldScale > 0.0f)
      ? 1.0f / camera.renderToWorldScale
      : 1.0f;
    labels.queryRadius = queryRadiusOriginal * worldToRender;
    changed = true;
  }
  changed |= ImGui::InputInt("number of particles", &labels.maxLabels);
  changed |= ImGui::Checkbox("sink particles only", &labels.sinkOnly);

  ImGui::TextDisabled("Search center: (%.3g, %.3g, %.3g)",
                      camera.originalTarget[0],
                      camera.originalTarget[1],
                      camera.originalTarget[2]);

  if (ImGui::Button("show particle IDs")) {
    labels.show = true;
    changed = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("disable particle IDs")) {
    labels.show = false;
    changed = true;
  }

  if (changed) {
    req.renderDraftDirty = true;
    req.applyRenderRequested = true;
  }
}

static void DrawCameraPlacementSection(SettingsRuntimeState& rt,
				       const SettingsCameraView& camera)
{
  auto& req = rt.cameraPlacement;

  ImGui::SeparatorText("Camera center");
  ImGui::InputFloat3("Center Coordinates", req.centerInput, "%.3f");
  if (ImGui::Button("Fill with Current Target")) {
    req.centerInput[0] = camera.originalTarget[0];
    req.centerInput[1] = camera.originalTarget[1];
    req.centerInput[2] = camera.originalTarget[2];
  }

  if (ImGui::Button("Set Center")) {
    req.setCenterRequested = true;
  }

  const char* viewDirections[] = {
    "View from +X", "View from -X",
    "View from +Y", "View from -Y",
    "View from +Z", "View from -Z"
  };
  ImGui::Combo("Projection Direction", &req.currentView, viewDirections, IM_ARRAYSIZE(viewDirections));

  ImGui::SliderFloat("Roll Angle (deg)", &req.rollAngle, -180.0f, 180.0f, "%.1f");

  if (ImGui::Button("Set Projection")) {
    req.setProjectionRequested = true;
  }

  ImGui::SeparatorText("View Culling");    
  ImGui::InputFloat("Culling radius", &rt.viewFilter.radiusCullingSphere, 0.f, 0.f, "%g");
  ImGui::InputFloat3("Culling center", &rt.viewFilter.center.x, "%.3f");

  if (ImGui::Button("Use Camera Target")) {
    rt.viewFilter.center = glm::vec3(camera.originalTarget[0],
                                     camera.originalTarget[1],
                                     camera.originalTarget[2]);
  }

  if (ImGui::Button("Apply Culling")) {
    req.applyCullingRequested = true;
  }
  
  if (ImGui::Button("Disable Culling")) {
    req.clearCullingRequested = true;
  }
}

static void DrawViewCameraSection(SettingsRuntimeState& rt,
                                  const SettingsCameraView& camera)
{
  if (!ImGui::CollapsingHeader("View / Camera"))
    return;

  DrawCameraPlacementSection(rt, camera);
}

#ifdef PYTHON_BRIDGE
static bool DrawPythonBridgeSection(SettingsPythonBridgeEdit& edit,
                                    const PythonBridgeViewState& view)
{
  if (!ImGui::CollapsingHeader("Python:Jupyter notebook"))
    return false;

  bool changed = false;
  const bool isOpen = view.available;

  if (ImGui::Button(isOpen ? "Close notebook" : "Open notebook")) {
    if (isOpen) {
      edit.shutdownClicked = true;
    } else {
      edit.launchClicked = true;
    }
    changed = true;
  }

  if (view.available) {
    ImGui::SameLine();
    ImGui::TextColored(view.launched ? ImVec4(0.6f,1,0.6f,1)
                                     : ImVec4(1,0.8f,0.4f,1),
                       view.launched ? "launched" : "launching...");
  }

  if (view.launched) {
    ImGui::SeparatorText("Jupyter Notebook");
    ImGui::Text("Port : %d", view.port);
    ImGui::TextWrapped("URL  : %s", view.url.c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy URL")) {
      ImGui::SetClipboardText(view.url.c_str());
    }

    if (ImGui::SmallButton("Open in Browser")) {
      edit.openBrowserClicked = true;
      changed = true;
    }

    if (!view.lastError.empty()) {
      ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", view.lastError.c_str());
    }
  }

  return changed;
}
#endif

static void DrawAnalysisSection(SettingsAnalysisEditState& edit,
                                const QuantityState& quantity,
                                const SettingsCameraView& camera,
                                const AnalysisJobRuntimeState& jobs,
                                FileNavigationRuntimeState& fileNav,
                                SnapshotFormatState& snapshotFormat,
                                const UnitSystem& units,
                                const SettingsAnalysisResultView& result,
                                SettingsUIState& ui,
                                WindowCommandQueue& windowCommands,
                                SettingsActionRequestState& settingsReq){
  if (!ImGui::CollapsingHeader("Analysis"))
    return;

  enum AnalysisMode {
    ANALYSIS_RADIAL_PROFILE,
    ANALYSIS_2D_HISTOGRAM,
    ANALYSIS_CLUMP_FIND,
    ANALYSIS_STELLAR_DENSITY, 
    ANALYSIS_HALO_CATALOGUE,
    ANALYSIS_POWER_SPEC,
    ANALYSIS_DISK,
    ANALYSIS_ISO_DENSITY,
    ANALYSIS_SNAPSHOT_EXTRACT
  };
		
  static PullDownItem analysisItems[] = {
    { "radial profile", ANALYSIS_RADIAL_PROFILE },
      { "2D histogram", ANALYSIS_2D_HISTOGRAM },
	{ "clump finder", ANALYSIS_CLUMP_FIND },
	{ "stellar density", ANALYSIS_STELLAR_DENSITY },
	{ "halo catalogue", ANALYSIS_HALO_CATALOGUE },
#ifdef POWER_SPECTRUM
	{ "power spectrum", ANALYSIS_POWER_SPEC },
#endif
#ifdef GEOMETRICAL_ANALYSIS
	{ "extract disks", ANALYSIS_DISK },
	{ "extract iso density", ANALYSIS_ISO_DENSITY },
#endif
    { "extract snapshot", ANALYSIS_SNAPSHOT_EXTRACT },
  };
		
  // Find the currently selected label.
  const char* currentLabel = "unknown";
  for (const auto& item : analysisItems) {
    if (item.mode == ui.analysisMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Analysis mode", currentLabel)) {
    for (const auto& item : analysisItems) {
      bool isSelected = (ui.analysisMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	ui.analysisMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
		
  switch (ui.analysisMode) {  
  case ANALYSIS_SNAPSHOT_EXTRACT: {
    DrawSnapshotExtractSection(fileNav,
                               snapshotFormat,
                               camera,
                               units,
                               settingsReq);
    break;
  }
  case ANALYSIS_RADIAL_PROFILE: {
    if (ImGui::Button("Compute radial profile"))
      windowCommands.open(WindowId::RadialProfile);
    break;
  }
  case ANALYSIS_2D_HISTOGRAM: {
    if (ImGui::Button("Compute 2D histogram"))
      windowCommands.open(WindowId::Histogram2D);
    break;
  }
  case ANALYSIS_CLUMP_FIND: {
    if (ImGui::Button("Run Clumps finder")) 
      windowCommands.open(WindowId::ClumpFinder);
    
#ifdef CLUMP_DATA_READ
    auto& batchReq = edit.clumpBatch;
    bool clumpBatchDirty = false;
    const auto& batchJob = jobs.clumpBatch.job;
    const auto* batchRes = result.clumpBatch;

    ImGui::Text("create clump data for continuous snapshots");

    clumpBatchDirty |= ImGui::RadioButton("FOF",        &batchReq.method, 0);
    ImGui::SameLine();
    clumpBatchDirty |= ImGui::RadioButton("Dendrogram", &batchReq.method, 1);

    clumpBatchDirty |= ImGui::InputInt("number of snapshots##FOF",
                                       &batchReq.nSnapshots);
    clumpBatchDirty |= ImGui::InputText("Output File Name##FOF",
                                        batchReq.outputFileName,
                                        IM_ARRAYSIZE(batchReq.outputFileName));
    clumpBatchDirty |= ImGui::InputText("Output Folder##FOF",
                                        batchReq.outputFolderPath,
                                        IM_ARRAYSIZE(batchReq.outputFolderPath));

    ImGui::SameLine();
    if (ImGui::Button("default path")) {
      std::strncpy(batchReq.outputFolderPath,
		   fileNav.input.folderPath,
		   IM_ARRAYSIZE(batchReq.outputFolderPath));
      batchReq.outputFolderPath[IM_ARRAYSIZE(batchReq.outputFolderPath) - 1] = '\0';
      clumpBatchDirty = true;
    }

    if (ImGui::Button("generate clump data")) {
      batchReq.generateClicked = true;
    }
    ImGui::SameLine();
    if (batchJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel clump batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d", batchJob.processed, batchReq.nSnapshots);
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed snapshots: %d", batchRes->processedSnapshots);
      ImGui::Text("Output: %s", batchRes->outputPath);
    }

    if (batchRes && batchRes->errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", batchRes->errorMessage);
    }

    if (ImGui::Button("show clump list")) {
      windowCommands.open(WindowId::ClumpList);
    }

    if (ImGui::Button("show clump chain list")) {
      windowCommands.open(WindowId::ClumpChain);
    }
    if (clumpBatchDirty) {
      edit.clumpBatchDirty = true;
    }
#endif
    break;
  }
  case ANALYSIS_STELLAR_DENSITY: {
    auto& req = edit.stellarDensity;
    bool stellarDensityDirty = false;

    ImGui::TextDisabled("Particle types to include");
    unsigned int selectedMask = 0;
    for (int type = 0; type < 6; ++type) {
      if (req.selectedTypes[type]) {
        selectedMask |= static_cast<unsigned int>(1u << type);
      }
    }
    if (DrawFramedTypeMaskToggleButtons(selectedMask, "stellar_density_type")) {
      for (int type = 0; type < 6; ++type) {
        const unsigned int bit = static_cast<unsigned int>(1u << type);
        req.selectedTypes[type] = (selectedMask & bit) != 0;
      }
      stellarDensityDirty = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Select 3,4,5##stellar_density")) {
      for (int t = 0; t < 6; ++t) req.selectedTypes[t] = false;
      req.selectedTypes[3] = true;
      req.selectedTypes[4] = true;
      req.selectedTypes[5] = true;
      stellarDensityDirty = true;
    }

    stellarDensityDirty |= ImGui::Checkbox("overwrite hsml##stellar_density",
                                           &req.overwriteHsml);

    if (ImGui::Button("Compute stellar density##stellar_density")) {
      req.computeClicked = true;
    }
    if (stellarDensityDirty) {
      edit.stellarDensityDirty = true;
    }

    break;
  }
#ifdef HAVE_HDF5
  case ANALYSIS_HALO_CATALOGUE: {
    if(ImGui::Button("Load Halo"))
      windowCommands.open(WindowId::Haloes);
    break;
  }
#endif
			
#ifdef POWER_SPECTRUM
  case ANALYSIS_POWER_SPEC: {
    auto& req = edit.powerSpectrum;
    const auto& spectrum = result.powerSpectrum;

    ImGui::SeparatorText("Power spectrum");
    const char* fieldKinds[] = {"scalar", "vector"};
    if (ImGui::Combo("field kind##power_spectrum",
                     &req.fieldKind,
                     fieldKinds,
                     IM_ARRAYSIZE(fieldKinds))) {
      edit.powerSpectrumDirty = true;
    }
    if (req.fieldKind == 0) {
      if (ImGui::BeginCombo("scalar quantity##power_spectrum",
                            QuantityDisplayLabel(quantity, req.scalarQuantity))) {
        for (int q = 0; q < quantity.catalog.nUIQ; ++q) {
          const QuantityId cand = quantity.catalog.uiQ[q];
          const bool selected = (cand == req.scalarQuantity);
          if (ImGui::Selectable(QuantityDisplayLabel(quantity, cand), selected)) {
            req.scalarQuantity = cand;
            edit.powerSpectrumDirty = true;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    } else {
      const char* fields[] = {"velocity", "B field"};
      if (ImGui::Combo("vector field##power_spectrum",
                       &req.vectorField,
                       fields,
                       IM_ARRAYSIZE(fields))) {
        edit.powerSpectrumDirty = true;
      }
    }
    if (ImGui::InputInt("grid size##power_spectrum", &req.gridSize)) {
      req.gridSize = std::clamp(req.gridSize, 8, 256);
      edit.powerSpectrumDirty = true;
    }
    if (ImGui::Checkbox("subtract mean##power_spectrum",
                        &req.subtractMean)) {
      edit.powerSpectrumDirty = true;
    }
    if (ImGui::Checkbox("Use analysis box##power_spectrum",
                        &req.useRegionBox)) {
      edit.powerSpectrumDirty = true;
    }
    if (req.useRegionBox) {
      ImGui::Indent();
      if (ImGui::SmallButton("camera center##power_spectrum_region")) {
        for (int axis = 0; axis < 3; ++axis) {
          req.regionCenter[axis] = camera.originalTarget[axis];
        }
        edit.powerSpectrumDirty = true;
      }
      if (ImGui::InputFloat3("center##power_spectrum_region",
                             req.regionCenter,
                             "%.3f")) {
        edit.powerSpectrumDirty = true;
      }
      if (ImGui::InputFloat("side length##power_spectrum_region",
                            &req.regionSideLength,
                            0.0f,
                            0.0f,
                            "%.3f")) {
        req.regionSideLength = std::max(0.0f, req.regionSideLength);
        edit.powerSpectrumDirty = true;
      }
      if (ImGui::Checkbox("show analysis box##power_spectrum_region",
                          &req.showRegionBox)) {
        edit.powerSpectrumDirty = true;
      }
      if (req.showRegionBox &&
          ImGui::SliderFloat("box opacity##power_spectrum_region",
                             &req.regionOpacity,
                             0.0f,
                             1.0f,
                             "%.2f")) {
        edit.powerSpectrumDirty = true;
      }
      ImGui::Unindent();
    }
    ImGui::SeparatorText("Axis for component spectra");
    if (ImGui::InputFloat3("axis tilt [deg]##power_spectrum_axis",
                           req.axisTiltDegrees,
                           "%.3f")) {
      SyncAxisVectorFromTilt(req.axisTiltDegrees, req.analysisAxis);
      edit.powerSpectrumDirty = true;
    }
    ImGui::Text("axis vector: (%.4f, %.4f, %.4f)",
                req.analysisAxis[0],
                req.analysisAxis[1],
                req.analysisAxis[2]);
    if (ImGui::SmallButton("set disk plane from angular momentum##power_spectrum_axis")) {
      req.setAxisFromAngularMomentumClicked = true;
      edit.powerSpectrumDirty = true;
    }
    ImGui::TextDisabled(
      "Vector spectra include axial/radial and toroidal/poloidal components.");

    if (ImGui::Button("Compute power spectrum##power_spectrum")) {
      req.computeClicked = true;
      edit.powerSpectrumDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##power_spectrum")) {
      req.clearClicked = true;
      edit.powerSpectrumDirty = true;
    }

    if (spectrum && !spectrum->result.message.empty()) {
      const ImVec4 color = spectrum->result.success
        ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
        : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
      ImGui::TextColored(color, "%s", spectrum->result.message.c_str());
    }

    if (spectrum && spectrum->computed && spectrum->result.success) {
      ImGui::Text("%s, grid: %d^3, samples: %d",
                  spectrum->result.fieldLabel.c_str(),
                  spectrum->result.gridSize,
                  spectrum->result.depositedSamples);
      if (spectrum->result.vectorSpectrum) {
        const char* plotGroups[] = {
          "radial / axial",
          "poloidal / toroidal",
          "compressive / solenoidal"
        };
        ImGui::Combo("plot group##power_spectrum",
                     &req.plotGroup,
                     plotGroups,
                     IM_ARRAYSIZE(plotGroups));
        req.plotGroup = std::clamp(req.plotGroup, 0,
                                   static_cast<int>(IM_ARRAYSIZE(plotGroups)) - 1);
      }
      std::vector<double> totalK, totalP;
      std::vector<double> solK, solP;
      std::vector<double> compK, compP;
      std::vector<double> axialK, axialP;
      std::vector<double> radialK, radialP;
      std::vector<double> toroidalK, toroidalP;
      std::vector<double> poloidalK, poloidalP;
      PlotPositiveRange xRange;
      PlotPositiveRange yRange;
      BuildPositivePlotSeries(spectrum->result.k,
                              spectrum->result.powerTotal,
                              totalK,
                              totalP,
                              xRange,
                              yRange);
      if (spectrum->result.vectorSpectrum) {
        if (req.plotGroup == 0) {
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerRadial,
                                  radialK,
                                  radialP,
                                  xRange,
                                  yRange);
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerAxial,
                                  axialK,
                                  axialP,
                                  xRange,
                                  yRange);
        } else if (req.plotGroup == 1) {
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerPoloidal,
                                  poloidalK,
                                  poloidalP,
                                  xRange,
                                  yRange);
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerToroidal,
                                  toroidalK,
                                  toroidalP,
                                  xRange,
                                  yRange);
        } else {
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerCompressive,
                                  compK,
                                  compP,
                                  xRange,
                                  yRange);
          BuildPositivePlotSeries(spectrum->result.k,
                                  spectrum->result.powerSolenoidal,
                                  solK,
                                  solP,
                                  xRange,
                                  yRange);
        }
      }
      ExpandLogRange(xRange);
      ExpandLogRange(yRange);
      if (ImPlot::BeginPlot("Power spectrum##power_spectrum",
                            ImVec2(-1, 320))) {
        ImPlot::SetupAxes("k", "P(k)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.3g");
        ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g");
        if (xRange.valid) {
          ImPlot::SetupAxisLimits(ImAxis_X1, xRange.min, xRange.max,
                                  ImGuiCond_Always);
        }
        if (yRange.valid) {
          ImPlot::SetupAxisLimits(ImAxis_Y1, yRange.min, yRange.max,
                                  ImGuiCond_Always);
        }
        if (!totalK.empty()) {
          ImPlot::PlotLine(spectrum->result.vectorSpectrum ? "total" : "scalar",
                           totalK.data(),
                           totalP.data(),
                           static_cast<int>(totalK.size()));
        }
        if (spectrum->result.vectorSpectrum) {
          if (req.plotGroup == 0 && !radialK.empty()) {
            ImPlot::PlotLine("radial",
                             radialK.data(),
                             radialP.data(),
                             static_cast<int>(radialK.size()));
          }
          if (req.plotGroup == 0 && !axialK.empty()) {
            ImPlot::PlotLine("axial",
                             axialK.data(),
                             axialP.data(),
                             static_cast<int>(axialK.size()));
          }
          if (req.plotGroup == 1 && !poloidalK.empty()) {
            ImPlot::PlotLine("poloidal",
                             poloidalK.data(),
                             poloidalP.data(),
                             static_cast<int>(poloidalK.size()));
          }
          if (req.plotGroup == 1 && !toroidalK.empty()) {
            ImPlot::PlotLine("toroidal",
                             toroidalK.data(),
                             toroidalP.data(),
                             static_cast<int>(toroidalK.size()));
          }
          if (req.plotGroup == 2 && !compK.empty()) {
            ImPlot::PlotLine("compressive",
                             compK.data(),
                             compP.data(),
                             static_cast<int>(compK.size()));
          }
          if (req.plotGroup == 2 && !solK.empty()) {
            ImPlot::PlotLine("solenoidal",
                             solK.data(),
                             solP.data(),
                             static_cast<int>(solK.size()));
          }
        }
        ImPlot::EndPlot();
      }
    }
    break;
  }
#endif
    
#ifdef GEOMETRICAL_ANALYSIS
  case ANALYSIS_DISK: {
    auto& singleReq = edit.disk;
    const auto* singleRes = result.disk;
    
    ImGui::SeparatorText("Single disk analysis");

    if (ImGui::InputScalar("Particle ID1##disk",
                           ImGuiDataType_S64,
                           &singleReq.targetParticleId)) {
      edit.diskDirty = true;
    }
    if (ImGui::SliderFloat("Opacity##disk",
                           &settingsReq.renderDraft.diskOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }

    if (ImGui::Button("Find a disk around the particle")) {
      singleReq.findClicked = true;
    }

    if (ImGui::Button("disable disks")) {
      singleReq.clearClicked = true;
    }

    if (singleRes && singleRes->valid) {
      ImGui::Text("Disk radius: %g", singleRes->radius);
    }

    auto& batchReq  = edit.diskBatch;
    bool diskBatchDirty = false;
    const auto& batchJob = jobs.diskBatch.job;
    const auto& batchRuntime = jobs.diskBatch;
    const auto* batchRes  = result.diskBatch;    
    ImGui::SeparatorText("Batch disk analysis");

    diskBatchDirty |= ImGui::InputText("Read target from text file##disk",
                                       batchReq.inputFile,
                                       IM_ARRAYSIZE(batchReq.inputFile));
    diskBatchDirty |= ImGui::InputText("Output target from text file##disk",
                                       batchReq.outputFile,
                                       IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("calc disk radius from text file")) {
      batchReq.runClicked = true;
    }
    if (batchJob.status == JobStatus::Running) {
      ImGui::SameLine();
      if (ImGui::Button("cancel disk batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d",
                  batchRuntime.rowCursor,
                  static_cast<int>(batchRuntime.rows.size()));
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed rows: %d", batchRes->processedRows);
    }
    if (diskBatchDirty) {
      edit.diskBatchDirty = true;
    }

    ImGui::Spacing();
    break;
  }

  case ANALYSIS_ISO_DENSITY: {
    auto& singleReq = edit.ellipsoid;
    const auto* singleRes = result.ellipsoid;
    auto& batchReq  = edit.ellipsoidBatch;
    bool ellipsoidBatchDirty = false;
    const auto& batchJob = jobs.ellipsoidBatch.job;
    const auto& batchRuntime = jobs.ellipsoidBatch;
    const auto* batchRes  = result.ellipsoidBatch;

    ImGui::SeparatorText("Single ellipsoid analysis");

    if (ImGui::InputScalar("Particle ID1",
                           ImGuiDataType_S64,
                           &singleReq.particleId1)) {
      edit.ellipsoidDirty = true;
    }
    if (ImGui::InputScalar("Particle ID2",
                           ImGuiDataType_S64,
                           &singleReq.particleId2)) {
      edit.ellipsoidDirty = true;
    }
    if (ImGui::SliderFloat("Opacity##contour_ellipse",
                           &settingsReq.renderDraft.ellipsoidOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }

    if (ImGui::Button("Fit Iso-density ellipsoid")) {
      singleReq.fitClicked = true;
    }

    if (ImGui::Button("disable Ellipsoid")) {
      singleReq.clearClicked = true;
    }

    if (singleRes && singleRes->valid) {
      ImGui::Text("a=%g b=%g c=%g",
		  singleRes->ellipsoid.radii.x,
		  singleRes->ellipsoid.radii.y,
		  singleRes->ellipsoid.radii.z);
    }

    ImGui::SeparatorText("Batch ellipsoid analysis");

    ellipsoidBatchDirty |= ImGui::InputText("Read target from text file",
                                            batchReq.inputFile,
                                            IM_ARRAYSIZE(batchReq.inputFile));
    ellipsoidBatchDirty |= ImGui::InputText("Output target from text file",
                                            batchReq.outputFile,
                                            IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("ellipsoidal fit from text file")) {
      batchReq.runClicked = true;
    }
    if (batchJob.status == JobStatus::Running) {
      ImGui::SameLine();
      if (ImGui::Button("cancel ellipsoid batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d",
                  batchRuntime.rowCursor,
                  static_cast<int>(batchRuntime.rows.size()));
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed rows: %d", batchRes->processedRows);
    }
    if (ellipsoidBatchDirty) {
      edit.ellipsoidBatchDirty = true;
    }

    ImGui::Spacing();
    break;
  }
#endif      
  }
}


static void DrawRenderingSection(const QuantityState& quantity,
				 SettingsAnalysisEditState& edit,
                                 const AnalysisJobRuntimeState& jobs,
                                 const SettingsAnalysisResultView& result,
                                 const SettingsCameraView& camera,
                                 SettingsUIState& ui,
                                 WindowCommandQueue& windowCommands,
				 SettingsActionRequestState& settingsReq){
  if (!ImGui::CollapsingHeader("Rendering"))
    return;

  const QuantityCatalogState& catalog = quantity.catalog;

  enum RenderingMode {
    RENDER_PROJECTION_MAP,
    RENDER_STREAM_LINE,
    RENDER_ISO_CONTOUR,
    RENDER_VOLUME,
    RENDER_VELOCITY_FIELD,
    RENDER_SINK_ID_VISUALIZATION
  };
		
  static PullDownItem renderingItems[] = {
    { "projection map", RENDER_PROJECTION_MAP },
#ifdef STREAM_LINE
    { "stream line", RENDER_STREAM_LINE },
#endif
#ifdef ISO_CONTOUR
    { "iso-contour", RENDER_ISO_CONTOUR },
#endif
#ifdef VOLUME_RENDERING
    { "adaptive volume", RENDER_VOLUME },
#endif
    { "velocity field", RENDER_VELOCITY_FIELD},
    { "sink ID visualization", RENDER_SINK_ID_VISUALIZATION },
  };
		
  // Find the currently selected label.
  const char* currentLabel = "unknown";
  for (const auto& item : renderingItems) {
    if (item.mode == ui.renderingMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Rendering mode", currentLabel)) {
    for (const auto& item : renderingItems) {
      bool isSelected = (ui.renderingMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	ui.renderingMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  switch (ui.renderingMode) {
  case RENDER_SINK_ID_VISUALIZATION: {
    DrawSinkIdSection(camera, settingsReq);
    break;
  }
  case RENDER_PROJECTION_MAP: {
    if (ImGui::Button("make projection map"))
      windowCommands.open(WindowId::ProjectionMap);
	    
    auto& movieReq = edit.projectionMovie;
    bool movieDirty = false;
    const auto& movieJob = jobs.projectionMovie.job;
    const auto* movieRes = result.projectionMovie;

    ImGui::Text("create projection maps for continuous snapshots");

    if (ImGui::InputInt("number of snapshots##render", &movieReq.nSnapshots)) {
      movieDirty = true;
    }
    if (movieReq.nSnapshots < 1) {
      movieReq.nSnapshots = 1;
      movieDirty = true;
    }
    movieDirty |= ImGui::InputText("Output File Format##render",
		                   movieReq.outputFileFormat,
		                   IM_ARRAYSIZE(movieReq.outputFileFormat));
    movieDirty |= ImGui::InputText("Output Folder##render",
		                   movieReq.outputFolderPath,
		                   IM_ARRAYSIZE(movieReq.outputFolderPath));
    movieDirty |= ImGui::InputText("Output Name of Movie##render",
		                   movieReq.outputMovieName,
		                   IM_ARRAYSIZE(movieReq.outputMovieName));

    movieDirty |= ImGui::Checkbox("restore camera after movie", &movieReq.restoreCameraOnFinish);
    ImGui::SeparatorText("Movie Tracking");
    movieDirty |= ImGui::Checkbox("follow the center around the sink particle", &movieReq.followSinkCenter);
    if (movieReq.followSinkCenter) {
      movieDirty |= ImGui::Checkbox("the most massive sink particle", &movieReq.followMostMassiveSink);
      if (!movieReq.followMostMassiveSink) {
	movieDirty |= ImGui::InputScalar("particle ID",
	                                 ImGuiDataType_S64,
	                                 &movieReq.particleIdCenter);
      }

      movieDirty |= ImGui::Checkbox("mass center around the particle", &movieReq.useMassCenter);
      if (movieReq.useMassCenter) {
	movieDirty |= ImGui::InputFloat("distance from the particle", &movieReq.massCenterRadius);
	movieDirty |= ImGui::InputFloat("the minimum density", &movieReq.massCenterMinDensity);
      }
    }

    ImGui::SeparatorText("Angular Momentum");
    movieDirty |= ImGui::Checkbox("force face-on view", &movieReq.faceOn);
    movieDirty |= ImGui::Checkbox("align camera to angular momentum", &movieReq.alignToAngularMomentum);

    const bool useAm = (movieReq.faceOn || movieReq.alignToAngularMomentum);
    if (useAm) {
      const char* amModes[] = {"Face-on", "Edge-on"};
      int amMode = movieReq.faceOn ? 0 : static_cast<int>(movieReq.amViewMode);
      if (ImGui::Combo("AM view mode", &amMode, amModes, IM_ARRAYSIZE(amModes)) && !movieReq.faceOn) {
        movieReq.amViewMode = static_cast<AngularMomentumViewMode>(amMode);
        movieDirty = true;
      }
      movieDirty |= ImGui::InputFloat("AM radius", &movieReq.amRadius, 0.f, 0.f, "%g");
      movieDirty |= ImGui::Checkbox("Subtract bulk velocity", &movieReq.amSubtractBulkVelocity);
      movieDirty |= ImGui::Checkbox("Keep axis sign continuity", &movieReq.amKeepSignContinuity);
      for (int t = 0; t < 6; ++t) {
        char label[64];
        std::snprintf(label, sizeof(label), "use type %d##movie_am_type", t);
        movieDirty |= ImGui::Checkbox(label, &movieReq.amUseType[t]);
        if (t < 5) ImGui::SameLine();
      }
    }

    if (movieJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel movie")) {
        movieReq.cancelClicked = true;
      }
      ImGui::SameLine();
      ImGui::Text("running %d / %d", movieJob.processed, movieReq.nSnapshots);
    } else if (ImGui::Button("generate maps")) {
      movieReq.cancelClicked = false;
      movieReq.generateClicked = true;
      edit.projectionMovieDirty = true;
    }

    if (movieDirty) {
      edit.projectionMovieDirty = true;
    }

    if (movieRes && movieRes->completed) {
      ImGui::Text("Processed snapshots: %d", movieRes->processedSnapshots);
      ImGui::Text("Movie: %s", movieRes->outputMoviePath);
    }

    if (movieRes && movieRes->errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", movieRes->errorMessage);
    }
    
    break;
  }
			
#ifdef STREAM_LINE
  case RENDER_STREAM_LINE: {
    auto& previewReq = edit.streamlinePreview;
    auto& buildReq   = edit.streamlineBuild;
    bool buildDirty = false;

    ImGui::Text("Seed setup");
    const char* fieldSources[] = {"velocity", "B field"};
    if (ImGui::Combo("vector field",
                     &buildReq.fieldSource,
                     fieldSources,
                     IM_ARRAYSIZE(fieldSources))) {
      buildDirty = true;
      buildReq.buildClicked = true;
    }
    buildDirty |= ImGui::InputInt("number of seed points", &buildReq.nSeeds);
    buildDirty |= ImGui::InputInt("max integration steps", &buildReq.maxSteps);
    buildDirty |= ImGui::InputFloat("step scale [hsml]",
                                    &buildReq.stepScale, 0.01f, 0.05f, "%.4f");
    buildDirty |= ImGui::InputFloat("curvature angle threshold [deg]",
                                    &buildReq.thetaMaxDegrees);
    if (ImGui::Checkbox("use manual seed", &buildReq.useManualSeed)) {
      buildDirty = true;
      buildReq.buildClicked = true;
    }

    if (buildReq.useManualSeed) {
      if (buildReq.manualSeeds.empty()) {
        buildReq.manualSeeds.push_back({0.f, 0.f, 0.f});
      }
      int removeSeed = -1;
      for (int i = 0; i < static_cast<int>(buildReq.manualSeeds.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::InputFloat3("manual seed position",
                               buildReq.manualSeeds[i].data(), "%.3f")) {
          buildDirty = true;
          buildReq.buildClicked = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) {
          removeSeed = i;
        }
        ImGui::PopID();
      }
      if (removeSeed >= 0 &&
          buildReq.manualSeeds.size() > 1) {
        buildReq.manualSeeds.erase(buildReq.manualSeeds.begin() + removeSeed);
        buildDirty = true;
        buildReq.buildClicked = true;
      }
      if (ImGui::Button("Add manual seed")) {
        buildReq.manualSeeds.push_back(buildReq.manualSeeds.back());
        buildDirty = true;
        buildReq.buildClicked = true;
      }
    }

    bool previewDirty = false;

    if (ImGui::InputFloat3("seed region center",
			   previewReq.seedCenter, "%.3f")) {
      previewDirty = true;
    }
    if (ImGui::InputFloat3("seed region side length",
			   previewReq.seedSize, "%.3f")) {
      previewDirty = true;
    }

    if (ImGui::SliderFloat("opacity##cubic",
			   &previewReq.opacity, 0.f, 1.f, "%.2f")) {
      previewDirty = true;
    }

    if (ImGui::SmallButton("camera center##streamline_seed_center")) {
      for (int axis = 0; axis < 3; ++axis) {
        previewReq.seedCenter[axis] = camera.originalTarget[axis];
      }
      previewDirty = true;
    }

    if (ImGui::Checkbox("show seed region box", &previewReq.showSeedBox)) {
      previewDirty = true;
    }

    if (previewDirty) {
      previewReq.updateClicked = true;
      edit.streamlinePreviewDirty = true;
    }

    ImGui::Text("Stream line setting");

    if (!buildReq.limitRegion) {
      for (int axis = 0; axis < 3; ++axis) {
        buildReq.regionCenter[axis] = previewReq.seedCenter[axis];
        buildReq.regionSize[axis] = previewReq.seedSize[axis];
      }
    }

    if (ImGui::Checkbox("limit stream lines in box", &buildReq.limitRegion)) {
      buildDirty = true;
    }

    if (buildReq.limitRegion) {
      buildDirty |= ImGui::InputFloat3("stream line region center",
                                       buildReq.regionCenter, "%.3f");
      buildDirty |= ImGui::InputFloat3("stream line region side length",
                                       buildReq.regionSize, "%.3f");
    }

    if (result.streamlineBuild &&
        !result.streamlineBuild->message.empty()) {
      const ImVec4 color = result.streamlineBuild->success
        ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
        : ImVec4(1.0f, 0.45f, 0.25f, 1.0f);
      ImGui::TextColored(color, "%s",
                         result.streamlineBuild->message.c_str());
      ImGui::Text("lines: %d / seeds: %d",
                  result.streamlineBuild->lineCount,
                  result.streamlineBuild->seedCount);
      static const char* stopLabels[] = {
        "none",
        "seed outside",
        "field eval failed",
        "weak field",
        "out of bounds",
        "zero step",
        "max steps"
      };
      for (int i = 0; i < 7; ++i) {
        const int count = result.streamlineBuild->stopCounts[i];
        if (count > 0) {
          ImGui::Text("  %s: %d", stopLabels[i], count);
        }
      }
      if (!result.streamlineBuild->seedReports.empty() &&
          ImGui::TreeNode("Seed details")) {
        for (const auto& seed : result.streamlineBuild->seedReports) {
          const int reason = (seed.stopReason >= 0 && seed.stopReason < 7)
            ? seed.stopReason
            : 0;
          ImGui::Text("#%d stop=%s points=%d length=%.6g position=(%.3f, %.3f, %.3f)",
                      seed.seedIndex,
                      stopLabels[reason],
                      seed.pointCount,
                      seed.length,
                      seed.position[0],
                      seed.position[1],
                      seed.position[2]);
        }
        ImGui::TreePop();
      }
    }

    if (ImGui::Button("Build stream lines")) {
      buildReq.buildClicked = true;
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      buildReq.clearClicked = true;
    }
    if (buildDirty) {
      edit.streamlineBuildDirty = true;
    }

    break;
  }    
#endif

#ifdef ISO_CONTOUR
  case RENDER_ISO_CONTOUR: {
    auto& req = edit.isoContour;
    bool isoContourDirty = false;

    ImGui::SeparatorText("Tree construction");

    if (ImGui::BeginCombo("Iso-contour quantity",
			  QuantityDisplayLabel(quantity, req.selectedQuantity))) {
      for (int q = 0; q < catalog.nUIQ; ++q) {
	QuantityId cand = catalog.uiQ[q];
	bool is_selected = (cand == req.selectedQuantity);
	if (ImGui::Selectable(QuantityDisplayLabel(quantity, cand), is_selected)) {
	  req.selectedQuantity = cand;
          isoContourDirty = true;
	}
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    isoContourDirty |= ImGui::InputInt("Min particles per leaf",
                                       &req.minParticlesPerLeaf);
    if (req.minParticlesPerLeaf < 1) {
      req.minParticlesPerLeaf = 1;
      isoContourDirty = true;
    }

    isoContourDirty |= ImGui::SliderInt("Max tree level",
                                        &req.maxTreeLevel,
                                        1,
                                        24);
    const char* reconstructionModes[] = {
      "Cell average (fast)",
      "Shared corners",
      "Face-gradient (slow)"
    };
    isoContourDirty |= ImGui::Combo("Corner reconstruction",
                                    &req.cornerReconstructionMode,
                                    reconstructionModes,
                                    IM_ARRAYSIZE(reconstructionModes));
    req.cornerReconstructionMode =
      std::clamp(req.cornerReconstructionMode, 0, 2);

    ImGui::TextDisabled("Changing tree parameters requires rebuilding the adaptive tree.");
    if (ImGui::Button("Build iso-contour tree")) {
      req.buildClicked = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear iso-contour tree")) {
      req.clearClicked = true;
    }

    ImGui::SeparatorText("Iso-surface extraction");
    ImGui::TextUnformatted("Apply threshold reuses the cached tree and rebuilds only the mesh.");
    isoContourDirty |= ImGui::InputFloat("Threshold value",
                                         &req.isoLevel);
    if (ImGui::Button("Apply threshold / rebuild mesh")) {
      req.applyClicked = true;
    }

    ImGui::SeparatorText("Rendering");
    if (ImGui::Button(settingsReq.renderDraft.showIsoContour
                      ? "Hide iso-contour"
                      : "Show iso-contour")) {
      settingsReq.renderDraft.showIsoContour =
        !settingsReq.renderDraft.showIsoContour;
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
    if (ImGui::SliderFloat("Iso-contour opacity",
                           &settingsReq.renderDraft.isoContourOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }

    if (result.isoContour && !result.isoContour->message.empty()) {
      ImGui::TextWrapped("%s", result.isoContour->message.c_str());
    }
    if (isoContourDirty) {
      edit.isoContourDirty = true;
    }

    break;
  }
#endif

#ifdef VOLUME_RENDERING
  case RENDER_VOLUME: {
    DrawVolumeRenderingSettingsSection(quantity, edit, result, settingsReq);
    break;
  }
#endif
			
	  case RENDER_VELOCITY_FIELD: {
    auto& velocity = settingsReq.renderDraft.velocity;
    if (ImGui::InputInt("show velocity field out of n particles", &velocity.subtraction)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
      settingsReq.velocityRenderDirtyRequested = true;
    }
    if (ImGui::InputFloat("Arrow Scale", &velocity.arrowScale, 0.1f, 1.0f, "%.2f")) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
    if (ImGui::Checkbox("Use Log Scale", &velocity.useLogScale)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
					
    if (ImGui::Checkbox("render velocity field", &velocity.show)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
      settingsReq.velocityRenderDirtyRequested = true;
    }
    break;
  }  
  }
}


static const char* ScaleGuideShapeLabel(ScaleGuideShapeType type)
{
  switch (type) {
  case ScaleGuideShapeType::Circle:
    return "Circle";
  case ScaleGuideShapeType::Square:
    return "Square";
  case ScaleGuideShapeType::Box:
    return "Box";
  }
  return "Circle";
}

static ScaleGuideObjectConfig MakeScaleGuideObject(ScaleGuideShapeType type,
                                                   const SettingsCameraView& camera)
{
  ScaleGuideObjectConfig object;
  object.type = type;
  object.center[0] = camera.originalTarget[0];
  object.center[1] = camera.originalTarget[1];
  object.center[2] = camera.originalTarget[2];
  return object;
}

static bool DrawScaleGuideObject(ScaleGuideObjectConfig& object,
                                 int index,
                                 const SettingsCameraView& camera)
{
  bool changed = false;
  const char* planeLabels[] = { "XY", "XZ", "YZ" };
  const char* circleModes[] = { "Powers of 10", "Fixed radius" };

  ImGui::PushID(index);

  changed |= ImGui::InputFloat3("Center", object.center, "%.4g");
  if (ImGui::Button("Move to camera center")) {
    object.center[0] = camera.originalTarget[0];
    object.center[1] = camera.originalTarget[1];
    object.center[2] = camera.originalTarget[2];
    changed = true;
  }

  if (object.type == ScaleGuideShapeType::Circle ||
      object.type == ScaleGuideShapeType::Square) {
    changed |= ImGui::Combo("Plane",
                            &object.plane,
                            planeLabels,
                            IM_ARRAYSIZE(planeLabels));
  }

  if (object.type == ScaleGuideShapeType::Circle) {
    changed |= ImGui::Combo("Mode",
                            &object.circleMode,
                            circleModes,
                            IM_ARRAYSIZE(circleModes));
    if (object.circleMode == 0) {
      ImGui::SetNextItemWidth(120.0f);
      changed |= ImGui::InputFloat("Min radius",
                                   &object.circleMinRadius,
                                   0.0f,
                                   0.0f,
                                   "%g");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120.0f);
      changed |= ImGui::InputFloat("Max radius",
                                   &object.circleMaxRadius,
                                   0.0f,
                                   0.0f,
                                   "%g");
    } else {
      changed |= ImGui::InputFloat("Radius",
                                   &object.circleFixedRadius,
                                   0.0f,
                                   0.0f,
                                   "%g");
    }
  } else if (object.type == ScaleGuideShapeType::Square) {
    changed |= ImGui::InputFloat("Square size",
                                 &object.squareSize,
                                 0.0f,
                                 0.0f,
                                 "%g");
  } else {
    changed |= ImGui::InputFloat3("Box size", object.boxSize, "%g");
  }

  ImGui::PopID();
  return changed;
}

static void DrawScaleGuideSection(ScaleGuideConfig& guide,
                                  const SettingsCameraView& camera)
{
  if (!ImGui::CollapsingHeader("Scale Guide"))
    return;

  bool changed = false;

  const char* shapeLabels[] = { "Circle", "Square", "Box" };
  ImGui::SetNextItemWidth(120.0f);
  ImGui::Combo("Add shape",
               &guide.addShapeType,
               shapeLabels,
               IM_ARRAYSIZE(shapeLabels));
  ImGui::SameLine();
  if (ImGui::Button("Add")) {
    guide.objects.push_back(MakeScaleGuideObject(
      static_cast<ScaleGuideShapeType>(guide.addShapeType),
      camera));
    changed = true;
  }

  for (size_t i = 0; i < guide.objects.size(); ) {
    ScaleGuideObjectConfig& object = guide.objects[i];
    char label[64];
    std::snprintf(label,
                  sizeof(label),
                  "%s %zu",
                  ScaleGuideShapeLabel(object.type),
                  i + 1);
    ImGui::SeparatorText(label);
    ImGui::PushID(static_cast<int>(i));
    changed |= ImGui::Checkbox("Enabled", &object.enabled);
    ImGui::SameLine();
    const float removeWidth = ImGui::CalcTextSize("Remove").x +
                              2.0f * ImGui::GetStyle().FramePadding.x;
    const float cursorX = ImGui::GetCursorPosX();
    const float targetX = std::max(cursorX,
                                   ImGui::GetContentRegionAvail().x +
                                     ImGui::GetCursorPosX() -
                                     removeWidth);
    ImGui::SetCursorPosX(targetX);
    if (ImGui::SmallButton("Remove")) {
      guide.objects.erase(guide.objects.begin() + static_cast<long>(i));
      changed = true;
      ImGui::PopID();
      continue;
    }
    ImGui::PopID();

    changed |= DrawScaleGuideObject(object, static_cast<int>(i), camera);
    ++i;
  }

  if (changed) {
    guide.dirty = true;
  }
}

static void DrawOtherSettingsSection(SettingsRuntimeState& rt,
                                     const SettingsCameraView& camera)
{
  if (!ImGui::CollapsingHeader("Other settings"))
    return;

  auto& req = rt.request;

  DrawScaleGuideSection(rt.scaleGuide, camera);

  if (ImGui::CollapsingHeader("Zoom Range")) {
    ImGui::InputFloat("Min Zoom", &rt.minZoom, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Max Zoom", &rt.maxZoom, 0.0f, 0.0f, "%g");
  }

  if (ImGui::CollapsingHeader("Normalization")) {
    DrawNormalizationSection(rt.normalization, req);
  }

  if (ImGui::CollapsingHeader("Render Overlays")) {
    bool dirty = false;
    dirty |= ImGui::Checkbox("Colorbar", &req.renderDraft.showColorbar);
    dirty |= ImGui::Checkbox("Coordinate axes", &req.renderDraft.showCoordAxes);
    dirty |= ImGui::Checkbox("Cross marker", &req.renderDraft.showCrossGizmo);
    if (dirty) {
      req.renderDraftDirty = true;
      req.applyRenderRequested = true;
    }
    if (ImGui::SliderFloat("Cross Marker Size",
                           &req.renderDraft.crossGizmoSize,
                           0.01f,
                           1.0f)) {
      req.renderDraftDirty = true;
      req.applyRenderRequested = true;
    }
  }
}
