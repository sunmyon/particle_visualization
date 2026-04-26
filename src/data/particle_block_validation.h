#pragma once

#include <cstddef>

struct ParticleBlock;

struct ParticleBlockValidationResult {
  bool valid = true;
  std::size_t particleIndex = 0;
  char message[256] = "";
};

ParticleBlockValidationResult ValidateParticleBlock(const ParticleBlock& block);
