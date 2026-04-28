#include "analysis/isosurface/iso_surface_field.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "analysis/isosurface/density_evaluate.h"

namespace {
struct CornerKey {
  long long x = 0;
  long long y = 0;
  long long z = 0;

  bool operator==(const CornerKey& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct CornerKeyHash {
  std::size_t operator()(const CornerKey& key) const noexcept {
    return static_cast<std::size_t>(key.x * 73856093ull) ^
           static_cast<std::size_t>(key.y * 19349663ull) ^
           static_cast<std::size_t>(key.z * 83492791ull);
  }
};

CornerKey QuantizeCorner(const glm::vec3& p)
{
  constexpr double eps = 1.0e-6;
  return {llround(static_cast<double>(p.x) / eps),
          llround(static_cast<double>(p.y) / eps),
          llround(static_cast<double>(p.z) / eps)};
}

glm::vec3 LeafCorner(const ParticleOctree::Node& leaf, int i)
{
  return glm::vec3((i & 1) ? leaf.box.max.x : leaf.box.min.x,
                   (i & 2) ? leaf.box.max.y : leaf.box.min.y,
                   (i & 4) ? leaf.box.max.z : leaf.box.min.z);
}
}

IsoSurfaceTreeField::IsoSurfaceTreeField(TrackingVector<IsoSurfaceLeafField> leaves)
  : leaves_(std::move(leaves))
{
  leafToIndex_.reserve(leaves_.size());
  for (std::size_t i = 0; i < leaves_.size(); ++i) {
    leafToIndex_[leaves_[i].leaf] = i;
  }
}

const IsoSurfaceLeafField* IsoSurfaceTreeField::find(const ParticleOctree::Node* leaf) const
{
  const auto it = leafToIndex_.find(leaf);
  if (it == leafToIndex_.end()) return nullptr;
  return &leaves_[it->second];
}

const std::array<float, 8>& IsoSurfaceTreeField::edgeValues(const ParticleOctree::Node* leaf) const
{
  static const std::array<float, 8> empty{};
  const IsoSurfaceLeafField* field = find(leaf);
  return field ? field->edgeValues : empty;
}

float IsoSurfaceTreeField::nearestCornerValue(const ParticleOctree::Node* leaf,
                                              const glm::vec3& position) const
{
  const IsoSurfaceLeafField* field = find(leaf);
  if (!field || !leaf) return 0.0f;

  float bestValue = field->edgeValues[0];
  double bestDist2 = std::numeric_limits<double>::infinity();
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner = LeafCorner(*leaf, i);
    const double dx = static_cast<double>(position.x - corner.x);
    const double dy = static_cast<double>(position.y - corner.y);
    const double dz = static_cast<double>(position.z - corner.z);
    const double dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 < bestDist2) {
      bestDist2 = dist2;
      bestValue = field->edgeValues[i];
    }
  }
  return bestValue;
}

TrackingVector<const ParticleOctree::Node*>
IsoSurfaceTreeField::leavesCrossing(float isoLevel) const
{
  TrackingVector<const ParticleOctree::Node*> out;
  out.reserve(leaves_.size());
  for (const IsoSurfaceLeafField& leaf : leaves_) {
    if (isoLevel >= leaf.minValue && isoLevel <= leaf.maxValue) {
      out.push_back(leaf.leaf);
    }
  }
  return out;
}

IsoSurfaceTreeField BuildIsoSurfaceTreeField(const ParticleOctree& tree)
{
  TrackingVector<ParticleDataForKdTree> sampleParticles;
  sampleParticles.reserve(tree.getParticles().size());
  for (const auto& p : tree.getParticles()) {
    sampleParticles.push_back({p.pos, p.val});
  }

  SPHInterpolator sph(std::move(sampleParticles));
  std::unordered_map<CornerKey, float, CornerKeyHash> sampledCorners;

  auto sampleCorner = [&](const glm::vec3& pos) {
    const CornerKey key = QuantizeCorner(pos);
    const auto found = sampledCorners.find(key);
    if (found != sampledCorners.end()) return found->second;

    const float value = sph.sample(pos);
    sampledCorners.emplace(key, value);
    return value;
  };

  TrackingVector<IsoSurfaceLeafField> fields;
  const auto leaves = tree.getAllLeafNodes();
  fields.reserve(leaves.size());

  for (const ParticleOctree::Node* leaf : leaves) {
    IsoSurfaceLeafField field;
    field.leaf = leaf;
    field.minValue = std::numeric_limits<float>::max();
    field.maxValue = -std::numeric_limits<float>::max();

    for (int i = 0; i < 8; ++i) {
      const float value = sampleCorner(LeafCorner(*leaf, i));
      field.edgeValues[i] = value;
      field.minValue = std::min(field.minValue, value);
      field.maxValue = std::max(field.maxValue, value);
    }
    fields.push_back(field);
  }

  return IsoSurfaceTreeField(std::move(fields));
}
