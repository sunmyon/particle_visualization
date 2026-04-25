#pragma once

#include <glm/glm.hpp>
#include "app/ui_state.h"

struct UnitSystem;
struct QuantityState;
struct QuantityCatalogState;
struct ParticleBlock;
struct CameraContext;
struct RenderLayerState;
struct NormalizationContext;
struct TrackingTargetState;
struct SnapshotPostprocessState;
struct ProjectionPreviewUIState;
struct ProjectionMapToolState;
class ParticleArray;
class HaloStore;

class RadialProfileComputer;
struct RadialProfileRuntimeState;
void OpenRadialProfileUI(RadialProfileUIState& state);
void DrawRadialProfileUI(RadialProfileUIState& state,
			 RadialProfileRuntimeState& rt,
			 RadialProfileComputer& computer,
                         const ParticleBlock& partblock,
			 const glm::vec3& cam_center,
			 NormalizationContext& normalization,
			 QuantityState& quantity);

class Histogram2DComputer;
struct Histogram2DContext;
struct Histogram2DRuntimeState;
void OpenHistogram2DUI(Histogram2DUIState& state);
void DrawHistogram2DUI(Histogram2DUIState& state,
		       Histogram2DRuntimeState& rt,
		       Histogram2DComputer& computer,
                       ParticleBlock& partblock,
		       const QuantityCatalogState& catalog,
		       const Histogram2DContext& ctx);

class ProjectionMapGenerator;
struct ProjectionMapUIState;
struct ProjectionMapParams;
struct CuboidObject;
void OpenProjectionMapUI(ProjectionMapUIState& state);
void DrawProjectionMapUI(ProjectionMapUIState& state,
			 ProjectionMapToolState& tool,
			 const ParticleArray& particles,
			 const NormalizationContext& normalization,
                         CameraContext& camCtx,
			 RenderLayerState& cuboidAnnotationState,
			 QuantityCatalogState& catalog,
                         int fileindex);

void DrawProjectionFontSelectionUI(ProjectionMapGenerator& generator,
				   ProjectionMapUIState& state);

void DrawTopParticlesUI(TopParticlesUIState& state, ParticleArray* P, CameraContext& camCtx, TrackingTargetState& track, SnapshotPostprocessState& post, UnitSystem& units);
void OpenHaloesUI(HaloesUIState& state);
void DrawHaloesUI(HaloesUIState& state, HaloStore& halo, CameraContext& camCtx, NormalizationContext& normalization);

void OpenMaskUI(MaskUIState& state);
bool DrawMaskWindow(MaskUIState& ui, ParticleMaskConfig& mask);

void DrawProjectionPreviewUI(const ProjectionPreviewUIState& st);

void ShowCameraSettingsUI();
void ShowTime(double time);
