#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include <vector>
#include "analysis/convex_hull/convex_hull_interface.h"

struct ConvexHullEntry {
  std::shared_ptr<IConvexHull> hull;
  std::string tag;
  int sourceId = -1;
  std::vector<float> lineVertices;
  bool visible = true;
};

struct ConvexHullRuntimeState {
  std::vector<ConvexHullEntry> entries;

  void clear() { entries.clear(); }
  void resetGroup(const std::string& tag) {
    entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [&](const ConvexHullEntry& e) { return e.tag == tag; }),
      entries.end());
  }

  std::vector<std::shared_ptr<IConvexHull>> visibleHulls() const {
    std::vector<std::shared_ptr<IConvexHull>> out;
    for (const auto& e : entries) {
      if (e.visible && e.hull) {
        out.push_back(e.hull);
      }
    }
    return out;
  } 
};
