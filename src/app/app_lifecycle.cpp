#include "app/state/app_state.h"
#include "app/app_lifecycle.h"
#include "app/app_snapshot_load.h"
#include "config/config_apply.h"
#include "config/config_data.h"
#include "config/config_extract.h"
#include "config/config_io.h"
#include "config/config_validation.h"

#include "render/render_system.h"

#include <memory>

#include "FileIO/snapshot_io_service.h"
#include "FindClumps/find_clumps.h"
#include "FindClumps/loaded_clump_tool.h"
#include "FindClumps/clump_chain.h"

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

static void InitAppServices(AppServices& services)
{
  services.projectionMap2D = std::make_unique<ProjectionMapGenerator>();
  services.snapshotIO      = std::make_unique<SnapshotIOService>();
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
  InitAppServices(app.services);

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
                    app.runtime.settings.fileNavigation,
                    app.runtime.settings.snapshotFormat,
                    app.runtime.quantity.units,
		    app.runtime.settings.normalization.desiredMax,
                    app.runtime.particleVisual,
                    app.runtime.settings.inputFilter.mask);
  }

  app.runtime.quantity.units.updateDerived();
  app.runtime.quantity.conversion.displaySpace =
    app.runtime.quantity.units.useComovingCoordinate
    ? UnitSpace::Comoving
    : UnitSpace::Physical;
  app.runtime.quantity.rebuildConversion(1.0);

  RequestSnapshotLoad(app.runtime.snapshotLoad,
                      SnapshotLoadOwner::UserNavigation,
                      app.runtime.settings.fileNavigation.navigation.currentStep,
                      100);
  ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);
}

void Cleanup(AppState& app, RenderSystem& rs)
{
#ifdef PYTHON_BRIDGE
  if (app.services.py.ptr) {
    app.services.py.ptr->shutdown();
    app.services.py.ptr.reset();
  }
#endif

  DestroyRenderSystem(rs);

  const ConfigData config =
    ExtractConfigData(app.runtime.settings.fileNavigation,
                      app.runtime.settings.snapshotFormat,
                      app.runtime.quantity.units,
		      app.runtime.settings.normalization.desiredMax,
                      app.runtime.particleVisual,
                      app.runtime.settings.inputFilter.mask);
  SaveConfigFile("config.txt", config);

  delete app.data.particles;
  app.data.particles = nullptr;
}
