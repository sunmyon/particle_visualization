#pragma once
#include <memory>
#include "core/tracking_vector.h"

class RadialProfileComputer;
class Histogram2DComputer;
class ProjectionMapGenerator;
class FindClump;
class DiskRadiusFinder;
class EllipsoidComputer;
class StreamlineComputer;
class EllipseFitter;

#ifdef VOLUME_RENDERING
#include "BVH/BVH.hpp"
#include "VolumeRendering/tau_sph.h"
#include "VolumeRendering/TransferFunctionEditor.hpp"
#include "VolumeRendering/OpacityComputer.hpp"

namespace lbvh { class MortonBuilder; }
class TransferFunctionEditor;

struct OctTreeCPUState {
  std::unique_ptr<ParticleOctree> cpuTree;

  std::vector<const ParticleOctree::Node*> order;
  std::vector<NodeInfo> info;
  std::unordered_map<const ParticleOctree::Node*, int> toIdx;

  uint64_t versionCPU = 0;
  bool dirtyCPU = true;
};

struct VolumeRenderingRuntime {
  lbvh::BuildResult bvhResult;
  RhoSigmaLUT rho2sigma;
  OctTreeCPUState octTree;
};
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

#ifdef USE_CONVEX_HULL
#include "geometry/convex_hull_generator.h"
#endif

struct AppServices {
  std::unique_ptr<RadialProfileComputer> radialProfile;
  std::unique_ptr<Histogram2DComputer> histogram2D;
  std::unique_ptr<ProjectionMapGenerator> projectionMap2D;
  std::unique_ptr<FindClump> clumpFind;
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
  #ifdef VOLUME_RENDERING
  std::unique_ptr<lbvh::MortonBuilder> bvh;
  std::unique_ptr<TransferFunctionEditor> tf;
  VolumeRenderingRuntime volume;
#endif
#ifdef PYTHON_BRIDGE
  PythonBridgeState py;
#endif
  
  AppServices();
  ~AppServices();
};
