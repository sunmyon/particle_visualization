#pragma once

#include <deque>
#include "ui_state.h"
#include "app/analysis_state.h"
#include "app/app_state.h"
#include "scene_manager.h"
#include "interaction/camera.h"
#include "render/object_renderer.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "make_2D_projection_map.h"

struct RadialProfileUIState {
  bool open = false;
  RadialProfileParams params;
  int selectedXAxis = 0;
  int selectedVarIdx = 0;
  bool computed = false;
  RadialProfileResult result;
};

void OpenRadialProfileUI();
void DrawRadialProfileUI(RadialProfileComputer& computer,
                         const ParticleBlock& partblock,
                         double unitmass_in_g,
                         double unitlength_in_cm,
                         double unittime_in_s);


struct Histogram2DUIState {
  bool open = false;
  bool computed = false;

  Histogram2DParams params;
  Histogram2DResult result;
};

void OpenHistogram2DUI();
void DrawHistogram2DUI(Histogram2DComputer& computer,
                       ParticleBlock& partblock,
		       const Histogram2DContext& ctx);


struct ProjectionMapUIState {
  bool open = false;
  bool useOriginalCoordinate = true;
  bool selectMode = false;

  bool fontWindowOpen = false;
  bool previewFontsInitialized = false;
  int currentFontIndex = 0;

  float xlen_input[3] = {2.0f, 2.0f, 1.0f};
  std::vector<ImFont*> previewFonts;
};

void OpenProjectionMapUI();
void DrawProjectionMapUI(ProjectionMapGenerator& generator,
                         ParticleArray* P,
                         CameraContext& camCtx,
			 RenderLayerState& cuboidAnnotationState,
                         int fileindex);


struct TopParticlesUIState {
  int queryID = -1;
  bool hasFound = false;
  ParticleData foundParticle{};

  int particleType = 3;
  int m = 10;

  std::deque<ParticleData> historyData;
  int historySel = -1;

  bool selectType[6] = {false, false, false, false, false, false};

  TrackingVector<ParticleData> filtered;

  int selectedVar = 4;
  int bins = 50;

  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool autoRange = true;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool useCameraCenter = false;
  float cameraRadius = 10.0f;

  bool histogramComputed = false;
  TrackingVector<float> histBins;
  TrackingVector<float> binCenters;

  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
};

void DrawTopParticlesUI(ParticleArray* P, CameraContext& camCtx);

struct HaloesUIState {
  bool open = false;
  char fname[255] = "";

  int m = 50;

  bool recomputeUseMassWeight = true;
  bool recomputeUseOriginalPos = true;
  int recomputeMinParticles = 20;

  int selectedVar = 0;
  int bins = 20;

  bool histogramLogScaleX = true;
  bool histogramLogScaleY = true;
  bool autoRange = true;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool histogramComputed = false;
  TrackingVector<float> histBins;
  TrackingVector<float> binCenters;
  float vmin = 0.0f;
  float vmax = 1.0f;
  float binSize = 1.0f;
};

class FileInfo;
void OpenHaloesUI();
void DrawHaloesUI(ParticleArray* P, CameraContext& camCtx, FileInfo* fileInfo);

struct MaskUIState {
  bool open = false;   // ★追加：表示/非表示

  // sphere
  bool  enableSphere = false;
  float center[3] = {0,0,0};
  float radius = 0.0f;

  enum class OutsideMode : int { Drop=0, Thin=1, KeepAll=2 };
  OutsideMode outsideMode = OutsideMode::Drop;
  int outsideStride = 10;

  enum class TypeMode : int { Off=0, On_NoThin=1, On_ThinOK=2 };
  TypeMode typeMode[6] = { TypeMode::On_ThinOK, TypeMode::On_ThinOK,
                           TypeMode::On_NoThin, TypeMode::On_NoThin,
                           TypeMode::On_NoThin, TypeMode::On_NoThin };

  bool enableMaxParticles = false;
  int  maxParticles = 2'000'000;

  bool autoApply = true;
  uint64_t revision = 0;
};

struct MaskConfig;
void OpenMaskUI();
bool DrawMaskWindow();
MaskConfig MakeMaskConfigFromUI();

#include "config/config_io.h"

void ExportMaskConfigState(ConfigMaskState& outState);
void ApplyMaskConfigState(const ConfigMaskState& state);

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
#ifdef ISO_CONTOUR
  IsoContourGeometryState* isoContour = nullptr;
#endif
};

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt);
