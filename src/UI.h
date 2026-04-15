#pragma once

#include <deque>
#include <glm/glm.hpp>

#include "app/runtime_state.h"
#include "app/analysis_state.h"
#include "app/app_state.h"
#include "app/ui_state.h"
#include "scene_manager.h"
#include "interaction/camera.h"
#include "render/object_renderer.h"
#include "make_2D_projection_map.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "core/units.h"

void OpenRadialProfileUI(RadialProfileUIState& state);

void DrawRadialProfileUI(RadialProfileUIState& state,
			 RadialProfileComputer& computer,
                         const ParticleBlock& partblock,
			 const glm::vec3& cam_center,
			 UnitSystem& units);

void OpenHistogram2DUI(Histogram2DUIState& state);

void DrawHistogram2DUI(Histogram2DUIState& state,
		       Histogram2DComputer& computer,
                       ParticleBlock& partblock,
		       const Histogram2DContext& ctx);

void OpenProjectionMapUI(ProjectionMapUIState& state);

void DrawProjectionMapUI(ProjectionMapUIState& state,
			 ProjectionMapGenerator& generator,
                         ParticleArray* P,
                         CameraContext& camCtx,
			 RenderLayerState& cuboidAnnotationState,
                         int fileindex);


void DrawTopParticlesUI(TopParticlesUIState& state, ParticleArray* P, CameraContext& camCtx);

void OpenHaloesUI(HaloesUIState& state);

class FileInfo;
void DrawHaloesUI(HaloesUIState& state, ParticleArray* P, CameraContext& camCtx, FileInfo* fileInfo);

void OpenMaskUI(MaskUIState& state);
bool DrawMaskWindow(MaskUIState& state);

struct ProjectionPreviewUIState {
  void* textureId = nullptr;   // ImTextureID 用
  int width = 0;
  int height = 0;
  bool valid = false;
};

class ProjectionMapGenerator;
void DrawProjectionPreviewUI(const ProjectionMapGenerator& gen,
                             const ProjectionPreviewUIState& st);

void ShowCameraSettingsUI();
void ShowTime(double time);

struct AppServices;
struct SettingsUIContext {
  ParticleArray* P = nullptr;
  FileInfo* fileInfo = nullptr;
  CameraContext* camCtx = nullptr;
  ParticleVisualConfig* particleVisual = nullptr;
  AppServices* services = nullptr;  
  RenderRuntimeState* render = nullptr;
  AnalysisDerivedState* analysis = nullptr;
};

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt);
