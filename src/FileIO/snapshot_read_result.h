#pragma once

#include "data/header_info.h"
#include "data/particle_block.h"

struct SnapshotReadResult {
  int fileIndex = -1;
  HeaderInfo header{};
  ParticleBlock block{};
};
