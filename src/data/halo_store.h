#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/tracking_vector.h"
#include "data/particle_data.h"
#include "data/halo_data.h"

class HaloStore {
public:
  void clear() {
    haloes_.clear();
    haloIDs_.clear();
    haloIDsLoaded_ = false;
  }
  
  bool empty() const { return haloes_.empty(); }
  std::size_t size() const { return haloes_.size(); }

  bool idsLoaded() const { return haloIDsLoaded_; }

  const TrackingVector<HaloData>& allHaloes() const { return haloes_; }
  TrackingVector<HaloData>& allHaloes() { return haloes_; }

  const HaloData& halo(std::size_t i) const { return haloes_[i]; }
  HaloData& halo(std::size_t i) { return haloes_[i]; }

  const std::vector<std::vector<uint64_t>>& allHaloIDs() const { return haloIDs_; }
  const std::vector<uint64_t>& ids(std::size_t i) const { return haloIDs_[i]; }

  void setHaloes(const TrackingVector<HaloData>& input) {
    haloes_ = input;
  }

  void setHaloes(TrackingVector<HaloData>&& input) {
    haloes_ = std::move(input);
  }

  void setHaloIDs(const std::vector<std::vector<uint64_t>>& input) {
    haloIDs_ = input;
    haloIDsLoaded_ = true;
  }

  void setHaloIDs(std::vector<std::vector<uint64_t>>&& input) {
    haloIDs_ = std::move(input);
    haloIDsLoaded_ = true;
  }

  void clearHaloIDs() {
    haloIDs_.clear();
    haloIDsLoaded_ = false;
  }

  bool hasUsableHaloIDs() const {
    return haloIDsLoaded_ && haloIDs_.size() == haloes_.size();
  }
  
  bool loadFromHDF5(const char* fname, bool loadIDs = true);
  
  void recomputeHaloPositionsFromParticles(const TrackingVector<ParticleData>& particles,
                                           bool useMassWeight,
                                           bool useOriginalPos);

private:
  TrackingVector<HaloData> haloes_;
  std::vector<std::vector<uint64_t>> haloIDs_;
  bool haloIDsLoaded_ = false;
};
