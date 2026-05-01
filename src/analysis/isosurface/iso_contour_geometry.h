#pragma once

#include <vector>
#include "core/quantity.h"
#include "volume/adaptive_volume_tree.h"

#include <string>

struct IsoContourGeometryState {
  std::vector<float> verts;
  std::vector<unsigned> inds;
  std::string message;
  AdaptiveVolumeTree adaptiveTree;
  bool adaptiveTreeValid = false;
  QuantityId cachedQuantity = QuantityId::Density;
  int cachedMaxTreeLevel = 0;
  int cachedMinParticlesPerLeaf = 0;
  int cachedCornerReconstructionMode = -1;

  bool cacheMatches(QuantityId quantity,
                    int maxTreeLevel,
                    int minParticlesPerLeaf,
                    int cornerReconstructionMode) const
  {
    return adaptiveTreeValid &&
           adaptiveTree.valid() &&
           cachedQuantity == quantity &&
           cachedMaxTreeLevel == maxTreeLevel &&
           cachedMinParticlesPerLeaf == minParticlesPerLeaf &&
           cachedCornerReconstructionMode == cornerReconstructionMode;
  }

  void clear() {
    verts.clear();
    inds.clear();
    message.clear();
    adaptiveTree.clear();
    adaptiveTreeValid = false;
  }
};
