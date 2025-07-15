#pragma once

#include<memory>
#include <openvdb/openvdb.h>

// 前方宣言で OpenVDB のヘッダを隠蔽
//namespace openvdb {
//class FloatGrid;          // ← これだけ前方宣言
//}

//using FloatGrid = openvdb::Grid<float>;
//using FloatGrid = openvdb::FloatGrid;

struct Particle { float x,y,z; float rho; float h; };
enum class GridMode { SPH, VORONOI };

// --- IsoContourBuilder Class ---
class IsoContourBuilder {
public:
  IsoContourBuilder();
  
  void setParticles(const TrackingVector<ParticleData>& pts);
  void buildGrid(void);
  void generateMesh(float isoLevel, TrackingVector<float>& outVerts, TrackingVector<unsigned>& outIndices);
  
  void setMode(GridMode m) {
    mode = m;
  };

  void resetGrid(float background = 0.0f);
  
private:
  static bool vdbInitialized;
  GridMode mode;
  TrackingVector<ParticleData> particles;
  std::shared_ptr<openvdb::FloatGrid> grid;
  void printVDBGridStats(openvdb::FloatGrid::Ptr grid);
};
