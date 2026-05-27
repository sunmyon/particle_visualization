#pragma once

#include "data/header_info.h"
#include "data/simulation_block.h"

#include <string>

struct SnapshotReadResult {
  int fileIndex = -1;
  std::string errorMessage;
  HeaderInfo header{};
  SimulationBlock block{};
};
