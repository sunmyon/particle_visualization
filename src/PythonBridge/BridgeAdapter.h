#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>
#include "PythonBridge/PythonBridge.h"  // PythonBridge::Shared, FieldId
#include "main.h"              // あなたの AoS 定義

namespace bridge {
  // AoS → SHM（初期一括コピー）
  bool loadInitialFromAoS(PythonBridge& bridge, const ParticleArray& P, size_t stride_bytes);
  
  // SHM → AoS（Python編集の反映。dirtyが空ならフル）
  void applyFromSharedToAoS(const PythonBridge::Shared& S, ParticleArray& P,
			    const std::vector<FieldId>& dirty = {});

} // namespace bridge
