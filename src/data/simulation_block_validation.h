#pragma once

#include <cstddef>

struct SimulationBlock;

struct SimulationBlockValidationResult {
  bool valid = true;
  std::size_t particleIndex = 0;
  char message[256] = "";
};

SimulationBlockValidationResult ValidateSimulationBlock(const SimulationBlock& block);
