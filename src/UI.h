#pragma once

#include <glm/glm.hpp>
#include "app/ui_state.h"

struct UnitSystem;
struct ParticleBlock;
struct CameraContext;
struct RenderLayerState;
struct NormalizationContext;
struct TrackingTargetState;
struct SnapshotPostprocessState;
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
			 UnitSystem& units);

class Histogram2DComputer;
struct Histogram2DContext;
struct Histogram2DRuntimeState;
void OpenHistogram2DUI(Histogram2DUIState& state);
void DrawHistogram2DUI(Histogram2DUIState& state,
		       Histogram2DRuntimeState& rt,
		       Histogram2DComputer& computer,
                       ParticleBlock& partblock,
		       const Histogram2DContext& ctx);

class ProjectionMapGenerator;
void OpenProjectionMapUI(ProjectionMapUIState& state);
void DrawProjectionMapUI(ProjectionMapUIState& state,
			 ProjectionMapGenerator& generator,
                         ParticleArray* P,
			 NormalizationContext& normalization,
                         CameraContext& camCtx,
			 RenderLayerState& cuboidAnnotationState,
                         int fileindex);

void DrawTopParticlesUI(TopParticlesUIState& state, ParticleArray* P, CameraContext& camCtx, TrackingTargetState& track, SnapshotPostprocessState& post);

class FileInfo;
void OpenHaloesUI(HaloesUIState& state);
void DrawHaloesUI(HaloesUIState& state, HaloStore& halo, CameraContext& camCtx, NormalizationContext& normalization);

void OpenMaskUI(MaskUIState& state);
bool DrawMaskWindow(MaskUIState& ui, ParticleMaskConfig& mask);

struct ProjectionPreviewUIState {
  void* textureId = nullptr;   // ImTextureID 用
  int width = 0;
  int height = 0;
  bool valid = false;
};

void DrawProjectionPreviewUI(const ProjectionMapGenerator& gen,
                             const ProjectionPreviewUIState& st);

void ShowCameraSettingsUI();
void ShowTime(double time);
