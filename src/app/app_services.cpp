#include "app_services.h"

#include "projection/make_2D_projection_map.h"
#include "FileIO/snapshot_io_service.h"
#include "analysis/clump/find_clumps.h"
#include "app/state/loaded_clump_tool.h"
#include "analysis/clump/clump_chain.h"

#ifdef GEOMETRICAL_ANALYSIS
#include "analysis/disk_radius.h"
#include "analysis/ellipse_fitter.h"
#endif

#ifdef STREAM_LINE
#include "analysis/streamline/streamline.h"
#endif

#ifdef USE_CONVEX_HULL
#include "analysis/convex_hull/convex_hull_generator.h"
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/PythonBridge.h"
PythonBridgeState::PythonBridgeState() = default;
PythonBridgeState::~PythonBridgeState() = default;
#endif

AppServices::AppServices() = default;
AppServices::~AppServices() = default;
