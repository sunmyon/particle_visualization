#pragma once

#include <array>
#include <cstddef>
#include <unordered_map>

#include "data/spatial/particle_octree.h"
#include "core/tracking_vector.h"

struct IsoSurfaceLeafField {
  const ParticleOctree::Node* leaf = nullptr;
  std::array<float, 8> edgeValues{};
  float minValue = 0.0f;
  float maxValue = 0.0f;
};

class IsoSurfaceTreeField {
public:
  explicit IsoSurfaceTreeField(TrackingVector<IsoSurfaceLeafField> leaves);

  const TrackingVector<IsoSurfaceLeafField>& leaves() const { return leaves_; }
  const IsoSurfaceLeafField* find(const ParticleOctree::Node* leaf) const;
  const std::array<float, 8>& edgeValues(const ParticleOctree::Node* leaf) const;
  float nearestCornerValue(const ParticleOctree::Node* leaf,
                           const glm::vec3& position) const;
  TrackingVector<const ParticleOctree::Node*> leavesCrossing(float isoLevel) const;

private:
  TrackingVector<IsoSurfaceLeafField> leaves_;
  std::unordered_map<const ParticleOctree::Node*, std::size_t> leafToIndex_;
};

IsoSurfaceTreeField BuildIsoSurfaceTreeField(const ParticleOctree& tree);
