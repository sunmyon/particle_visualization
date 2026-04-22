#include "app/app_state.h"
#include "app/app_lifecycle.h"
#include "app/app_callbacks.h"
#include "config/config_apply.h"
#include "config/config_data.h"
#include "config/config_extract.h"
#include "config/config_io.h"
#include "config/config_validation.h"

#include "render/render_system.h"
#include "window_context.h"

#include <iostream>
#include <memory>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#endif

#include "imgui_context.h"
#include "UI.h"

#include "FileIO/file_io.h"
#include "FindClumps/find_clumps.h"
#include "FindClumps/loaded_clump_tool.h"
#include "FindClumps/clump_chain.h"

#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "projection/make_2D_projection_map.h"

#ifdef USE_CONVEX_HULL
#include "geometry/convex_hull_generator.h"
#endif

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

static void InitAppServices(AppServices& services)
{
  services.radialProfile   = std::make_unique<RadialProfileComputer>();
  services.histogram2D     = std::make_unique<Histogram2DComputer>();
  services.projectionMap2D = std::make_unique<ProjectionMapGenerator>();
  services.clumpFind       = std::make_unique<FindClump>();
  services.clumpLoad       = std::make_unique<LoadedClumpTool>();
  services.clumpChain      = std::make_unique<ClumpChain>();
#ifdef USE_CONVEX_HULL
  services.convexHull      = std::make_unique<ConvexHullGenerator>();
#endif
#ifdef GEOMETRICAL_ANALYSIS
  services.diskFinder      = std::make_unique<DiskRadiusFinder>();
  services.ellipsoid       = std::make_unique<EllipseFitter>();
#endif
#ifdef STREAM_LINE
  services.streamLine      = std::make_unique<StreamlineComputer>();
#endif
}

void InitApplication(AppState& app, RenderSystem& render)
{
  InitRenderPrograms(render.programs);
  InitAppServices(app.services);

  app.data.fileInfo  = new FileInfo();
  app.data.particles = new ParticleArray();

  InitRenderSystem(render);
}



void LoadInitialData(AppState& app)
{
  ConfigData config;
  if (LoadConfigFile("config.txt", config)) {
    ConfigValidationIssues issues;
    SanitizeConfigData(config, &issues);
    PrintConfigValidationIssues(issues);
    
    ApplyConfigData(config,
                    *app.data.fileInfo,
                    app.data.particles->units,
		    app.runtime.settings.normalization.desiredMax,
                    app.view.particleVisual,
                    app.runtime.settings.inputFilter.mask);
  }

  app.data.particles->units.updateDerived();
  app.data.fileInfo->setUnit(app.data.particles->units);

  const auto& src = app.data.fileInfo->getSource();
  
  const int newFileIndex = src.initialIndex + src.currentStep * src.skipStep;
  app.data.fileInfo->loadNewSnapshot(newFileIndex, app.data.particles, app.runtime.settings.normalization, app.runtime.settings.inputFilter);
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

  const ConfigData config =
    ExtractConfigData(*app.data.fileInfo,
                      app.data.particles->units,
		      app.runtime.settings.normalization.desiredMax,
                      app.view.particleVisual,
                      app.runtime.settings.inputFilter.mask);
  SaveConfigFile("config.txt", config);

#ifndef NONATIVEFILEDIALOG
  NFD_Quit();
#endif

  rs.preview.destroy();
  ShutdownImGuiContext();
  window.destroy();

  delete app.data.particles;
  app.data.particles = nullptr;

  delete app.data.fileInfo;
  app.data.fileInfo = nullptr;
}
