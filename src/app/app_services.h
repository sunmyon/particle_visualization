#pragma once
#include <memory>
#include <vector>

class ProjectionMapGenerator;
class SnapshotIOService;
class FindClump;
class LoadedClumpTool;
class ClumpChain;
class DiskRadiusFinder;
class EllipsoidComputer;
class StreamlineComputer;
class EllipseFitter;
#ifdef USE_CONVEX_HULL
class ConvexHullGenerator;
#endif

#ifdef PYTHON_BRIDGE
class PythonBridge;
struct PythonBridgeState {
  std::unique_ptr<PythonBridge> ptr;
  bool launched = false;
  bool needUploadPos = false;

  PythonBridgeState();
  ~PythonBridgeState();
};
#endif

struct AppServices {
  std::unique_ptr<ProjectionMapGenerator> projectionMap2D;
  std::unique_ptr<SnapshotIOService> snapshotIO;
  std::unique_ptr<FindClump> clumpFind;
  std::unique_ptr<LoadedClumpTool> clumpLoad;
  std::unique_ptr<ClumpChain> clumpChain;
#ifdef USE_CONVEX_HULL
  std::unique_ptr<ConvexHullGenerator> convexHull;
#endif
#ifdef GEOMETRICAL_ANALYSIS
  std::unique_ptr<DiskRadiusFinder> diskFinder;
  std::unique_ptr<EllipseFitter> ellipsoid;
#endif
#ifdef STREAM_LINE
  std::unique_ptr<StreamlineComputer> streamLine;
#endif
#ifdef PYTHON_BRIDGE
  PythonBridgeState py;
#endif
  
  AppServices();
  ~AppServices();
};
