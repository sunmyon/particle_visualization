#pragma once
#include "ParticleOctree.h"
#include "marching_cubes.h"
#include "mesh_data.h"

/// 連結性まわりのデバッグテストを走らせる
/// epsCorner … 頂点値比較の許容誤差
/// epsQuant  … 頂点量子化幅（重複判定に使用）
void runConnectivityTests(const ParticleOctree&   tree,
                          const Mesh&             mesh,
                          float                   epsCorner = 1e-5f,
                          float                   epsQuant  = 1e-5f);

void runConnectivityQuickCheck(const ParticleOctree&   tree,
			       const Mesh&             mesh,
			       float isoLevel,
			       float                   epsCorner = 1e-5f,
			       float                   epsQuant  = 1e-5f);
