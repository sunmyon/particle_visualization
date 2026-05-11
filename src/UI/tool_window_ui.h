#pragma once

#include <array>
#include <cstddef>

#include "app/state/plot_export_state.h"

struct Histogram2DRequestState;
struct Histogram2DResultState;
struct Histogram2DUIState;
struct MaskRequestState;
struct MaskUIState;
struct CameraContext;
struct NormalizationContext;
struct ParticleMaskConfig;
struct ProjectionFontSelectionRequestState;
struct ProjectionMapRequestState;
struct ProjectionMapToolState;
struct ProjectionMapUIState;
struct ProjectionPreviewUIState;
struct QuantityCatalogState;
struct QuantityState;
struct RadialProfileRequestState;
struct RadialProfileResultState;
struct RadialProfileUIState;
struct TopParticlesRequestState;
struct TopParticlesResultState;
struct TopParticlesUIState;
struct TopParticlesViewContext;
struct HaloesRequestState;
struct HaloesUIState;
struct WindowCommandQueue;

struct RadialProfileViewContext {
  const QuantityState& quantity;
  PlotBatchExportViewContext exportContext;
};

void DrawRadialProfileUI(RadialProfileUIState& state,
                         RadialProfileRequestState& request,
                         const RadialProfileResultState& result,
                         const RadialProfileViewContext& ctx);

struct Histogram2DViewContext {
  const QuantityCatalogState& catalog;
  std::array<std::size_t, 6> particleTypeCounts{};
  PlotBatchExportViewContext exportContext;
};

void DrawHistogram2DUI(Histogram2DUIState& state,
                       Histogram2DRequestState& request,
                       const Histogram2DResultState& result,
                       const Histogram2DViewContext& ctx);

struct ProjectionMapViewContext {
  WindowCommandQueue& windowCommands;
  const ProjectionMapToolState& tool;
  const NormalizationContext& normalization;
  const QuantityState& quantity;
  const CameraContext& camera;
};

void DrawProjectionMapUI(ProjectionMapUIState& state,
                         ProjectionMapRequestState& request,
                         const ProjectionMapViewContext& ctx);

void DrawProjectionFontSelectionUI(ProjectionMapUIState& state,
                                   ProjectionFontSelectionRequestState& request);

void DrawTopParticlesUI(TopParticlesUIState& state,
                        TopParticlesRequestState& request,
                        const TopParticlesResultState& result,
                        const TopParticlesViewContext& ctx);

struct HaloesViewContext {
  PlotBatchExportViewContext exportContext;
};

void DrawHaloesUI(HaloesUIState& state,
                  HaloesRequestState& request,
                  const HaloesViewContext& ctx);

bool DrawMaskWindow(MaskUIState& ui,
                    MaskRequestState& request,
                    ParticleMaskConfig& mask);

void DrawProjectionPreviewUI(ProjectionMapUIState& state,
                             const ProjectionPreviewUIState& st);
