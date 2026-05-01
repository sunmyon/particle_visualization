#pragma once

#include "data/header_info.h"
#include "data/simulation_block.h"

struct SnapshotReadResult {
  int fileIndex = -1;
  HeaderInfo header{};
  SimulationBlock block{};
};
