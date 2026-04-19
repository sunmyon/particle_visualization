#pragma once
#include "data/particle_block.h"
#include "core/tracking_vector.h"

class PrefetchCache {
public:
  struct Entry {
    int fileIndex = -1;
    ParticleBlock block;
  };

  void clear() {
    entries_.clear();
  }

  void push(int fileIndex, ParticleBlock&& block) {
    Entry e;
    e.fileIndex = fileIndex;
    e.block = std::move(block);
    entries_.push_back(std::move(e));
  }

  bool pop(int fileIndex, ParticleBlock& out) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->fileIndex == fileIndex) {
        out = std::move(it->block);
        entries_.erase(it);
        return true;
      }
    }
    return false;
  }

private:
  TrackingVector<Entry> entries_;
};
