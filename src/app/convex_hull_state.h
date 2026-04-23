#pragma once

#include <memory>
#include <string>

#include "core/tracking_vector.h"
#include "geometry/convex_hull_interface.h"

struct ConvexHullEntry {
  std::shared_ptr<IConvexHull> hull;
  std::string tag;
  int sourceId = -1;
  TrackingVector<float> lineVertices;
  bool visible = true;
};

struct ConvexHullRuntimeState {
  TrackingVector<ConvexHullEntry> entries;

  void clear() { entries.clear(); }
  void resetGroup(const std::string& tag) {
    entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [&](const ConvexHullEntry& e) { return e.tag == tag; }),
      entries.end());
  }

  TrackingVector<std::shared_ptr<IConvexHull>> visibleHulls() const {
    TrackingVector<std::shared_ptr<IConvexHull>> out;
    for (const auto& e : entries) {
      if (e.visible && e.hull) {
        out.push_back(e.hull);
      }
    }
    return out;
  } 
};
