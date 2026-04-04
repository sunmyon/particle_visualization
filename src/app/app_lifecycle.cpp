#include "app/app_lifecycle.h"
#include "app/app_callbacks.h"

#include <iostream>
#include <memory>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#endif

#include "imgui_context.h"
#include "UI.h"

#include "FindClumps/find_clumps.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "make_2D_projection_map.h"

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/ellipse_fitter.h"
#include "GeometricAnalysis/DiskRadius.hpp"
#endif

#ifdef STREAM_LINE
#include "StreamLine/stream_line_new.h"
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/PythonBridge.h"
#endif

bool InitPlatform(WindowContext& window,
		  CallbackContext& callbackCtx,
		  AppState& app)
{
  if (!window.init(1280, 720, "3D Particle Visualization")) {
    return false;
  }

  callbackCtx.app = &app;
  callbackCtx.window = &window;

  glfwSetWindowUserPointer(window.handle(), &callbackCtx);

  window.attachCallbacks(mouse_callback,
                         scroll_callback,
                         framebuffer_size_callback);

  InitImGuiContext(window.handle());

#ifndef NONATIVEFILEDIALOG
  if (NFD_Init() != NFD_OKAY) {
    std::cerr << "NFD_Init failed: " << NFD_GetError() << std::endl;
  }
#endif

  return true;
}

static void InitAppServices(AppServices& services, CameraContext& camera)
{
  services.radialProfile   = std::make_unique<RadialProfileComputer>(camera.cameraTarget);
  services.histogram2D     = std::make_unique<Histogram2DComputer>(camera.cameraTarget);
  services.projectionMap2D = std::make_unique<ProjectionMapGenerator>();
  services.clumpFind       = std::make_unique<FindClump>(camera);
#ifdef GEOMETRICAL_ANALYSIS
  services.diskFinder      = std::make_unique<DiskRadiusFinder>();
  services.ellipsoid       = std::make_unique<EllipseFitter>();
#endif
#ifdef STREAM_LINE
  services.streamLine      = std::make_unique<StreamlineComputer>();
#endif
#ifdef VOLUME_RENDERING
  services.bvh             = std::make_unique<lbvh::MortonBuilder>();
  services.tf              = std::make_unique<TransferFunctionEditor>();
#endif
}

void InitApplication(AppState& app, RenderSystem& render)
{
  InitRenderPrograms(render.programs);
  InitAppServices(app.services, app.camera);

  app.fileInfo  = new FileInfo(app.camera);
  app.particles = new ParticleArray(app.camera);

  InitRenderSystem(render);

#ifdef USE_CONVEX_HULL
  app.convexHullGenerator = new ConvexHullGenerator();
#endif
}



void LoadInitialData(AppState& app)
{
  ConfigMaskState maskState;
  if (loadConfig("config.txt",
                 app.particles,
                 app.fileInfo,
                 &app.particleVisual,
                 &maskState)) {
    ApplyMaskConfigState(maskState);

    MaskConfig cfg = MakeMaskConfigFromUI();
    app.fileInfo->setMaskConfig(cfg);
  }

  app.fileInfo->setUnit(app.particles);

  const int newFileIndex =
    app.fileInfo->initialIndex +
    app.fileInfo->currentStep * app.fileInfo->skipStep;

  app.fileInfo->loadBatch(newFileIndex,
                          app.fileInfo->batchSize,
                          app.fileInfo->skipStep,
                          app.particles);
}

void Cleanup(AppState& app, RenderSystem& rs, WindowContext& window)
{
#ifdef PYTHON_BRIDGE
  if (app.services.py.ptr) {
    app.services.py.ptr->shutdown();
    app.services.py.ptr.reset();
  }
#endif

  DestroyRenderSystem(rs);
  DestroyRenderPrograms(rs.programs);

  ConfigMaskState maskState;
  ExportMaskConfigState(maskState);
  saveConfig("config.txt",
             app.particles,
             app.fileInfo,
             &app.particleVisual,
             &maskState);

#ifndef NONATIVEFILEDIALOG
  NFD_Quit();
#endif

  rs.preview.destroy();
  ShutdownImGuiContext();
  window.destroy();

#ifdef USE_CONVEX_HULL
  delete app.convexHullGenerator;
  app.convexHullGenerator = nullptr;
#endif

  delete app.particles;
  app.particles = nullptr;

  delete app.fileInfo;
  app.fileInfo = nullptr;
}
