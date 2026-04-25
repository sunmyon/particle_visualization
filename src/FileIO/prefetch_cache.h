#pragma once
#include "FileIO/snapshot_read_result.h"
#include "core/tracking_vector.h"

class PrefetchCache {
public:
  struct Entry {
    SnapshotReadResult result;
  };

  void clear() {
    entries_.clear();
  }

  void push(SnapshotReadResult&& result) {
    Entry e;
    e.result = std::move(result);
    entries_.push_back(std::move(e));
  }

  bool pop(int fileIndex, SnapshotReadResult& out) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->result.fileIndex == fileIndex) {
        out = std::move(it->result);
        entries_.erase(it);
        return true;
      }
    }
    return false;
  }

private:
  TrackingVector<Entry> entries_;
};
