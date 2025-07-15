// OpenVDB
#include <openvdb/openvdb.h>
#include <openvdb/tools/VolumeToMesh.h>

#include "main.h"
#include "iso_contour.h"

bool IsoContourBuilder::vdbInitialized = false;

IsoContourBuilder::IsoContourBuilder()
{
  if (!vdbInitialized) {
    openvdb::initialize();
    vdbInitialized = true;
  }
  
  grid = openvdb::FloatGrid::create(0.0f);
  grid->setTransform(openvdb::math::Transform::createLinearTransform(0.1));
  grid->setGridClass(openvdb::GRID_FOG_VOLUME);
}

// --- IsoContourBuilder Class ---
void IsoContourBuilder::setParticles(const TrackingVector<ParticleData>& pts) {
  particles = pts;
}

void IsoContourBuilder::resetGrid(float background) {
  //grid->setBackground(background);
  grid->tree().clear();
}


void IsoContourBuilder::buildGrid() {
  auto acc = grid->getAccessor();
  for (auto& p : particles) {
    openvdb::Vec3R P(p.pos[0],p.pos[1],p.pos[2]);
    openvdb::Vec3d idx = grid->transform().worldToIndex(P);
    openvdb::Coord ijk = openvdb::Coord::floor(idx);
    
    if (mode == GridMode::SPH) {
      float support = 2.0f * p.Hsml;
      openvdb::Vec3R rad(support);
      
      openvdb::Vec3d idx_min = grid->transform().worldToIndex(P - rad);      
      openvdb::Coord bbMin = openvdb::Coord::floor(idx_min);

      openvdb::Vec3d idx_max = grid->transform().worldToIndex(P + rad);      
      openvdb::Coord bbMax = openvdb::Coord::floor(idx_max);

      for(int i = bbMin.x(); i <= bbMax.x(); ++i)
	for(int j = bbMin.y(); j <= bbMax.y(); ++j)
	  for(int k = bbMin.z(); k <= bbMax.z(); ++k) {
	    openvdb::Coord c(i,j,k);
	    openvdb::Vec3R pos = grid->transform().indexToWorld(c);
	    float r = (pos - P).length() / p.Hsml;
	    float w = (r < 1.0f) ? 1.0f - 1.5f*r*r + 0.75f*r*r*r
	      : (r < 2.0f) ? 0.25f*std::pow(2.0f - r, 3.0f)
	      : 0.0f;
	    if (w > 0.0f) {
	      acc.setValue(c, acc.getValue(c) + p.density * w);
	    }
	  }
    } else {
      acc.setValue(ijk, acc.getValue(ijk) + p.density);
    }
  }

  printVDBGridStats(grid);
}

void IsoContourBuilder::generateMesh(float isoLevel,  TrackingVector<float>& outVerts, TrackingVector<unsigned>& outIndices){
  std::vector<openvdb::Vec3s> points;
  std::vector<openvdb::Vec3I> tris;
  std::vector<openvdb::Vec4I> quads;

  openvdb::tools::volumeToMesh(*grid, points, tris, quads, isoLevel);

  outVerts.clear();
  outIndices.clear();

  outVerts.reserve(points.size() * 3);
  for (const auto& p : points) {
    outVerts.push_back(p.x());
    outVerts.push_back(p.y());
    outVerts.push_back(p.z());
  }

  // 三角形
  outIndices.reserve((tris.size() + 2 * quads.size()) * 3); // 予測サイズも修正
  for (const auto& t : tris) {
    outIndices.push_back(static_cast<unsigned>(t[0]));
    outIndices.push_back(static_cast<unsigned>(t[1]));
    outIndices.push_back(static_cast<unsigned>(t[2]));
  }

  // 四角形 → 三角形に分割
  for (const auto& q : quads) {
    outIndices.push_back(static_cast<unsigned>(q[0]));
    outIndices.push_back(static_cast<unsigned>(q[1]));
    outIndices.push_back(static_cast<unsigned>(q[2]));

    outIndices.push_back(static_cast<unsigned>(q[0]));
    outIndices.push_back(static_cast<unsigned>(q[2]));
    outIndices.push_back(static_cast<unsigned>(q[3]));
  }

  std::cout << "Verts: " << outVerts.size() / 3
            << ", Indices: " << outIndices.size() << std::endl;

  printVDBGridStats(grid);
}

#include <openvdb/tools/Statistics.h>  // ← 統計を取るためのユーティリティ

void IsoContourBuilder::printVDBGridStats(openvdb::FloatGrid::Ptr grid)
{
  if (!grid) {
        std::cerr << "Grid is null!" << std::endl;
        return;
    }

    std::cout << "=== OpenVDB Grid Info ===" << std::endl;

    std::cout << "Grid name: " << grid->getName() << std::endl;
    std::cout << "Voxel size: " << grid->voxelSize() << std::endl;

    const auto& tree = grid->tree();

    std::cout << "Total active voxel count: " << tree.activeVoxelCount() << std::endl;
    std::cout << "Active leaf node count: " << tree.leafCount() << std::endl;

    // 代替として bounding box を出すことで範囲を知る
    openvdb::CoordBBox bbox;
    if (tree.evalActiveVoxelBoundingBox(bbox)) {
        std::cout << "Active voxel bounding box: " << bbox << std::endl;
        std::cout << "Extent: (" 
                  << bbox.dim().x() << ", "
                  << bbox.dim().y() << ", "
                  << bbox.dim().z() << ")" << std::endl;
    } else {
        std::cout << "No active voxels found." << std::endl;
    }

    std::cout << "=== End Grid Info ===" << std::endl;
}
