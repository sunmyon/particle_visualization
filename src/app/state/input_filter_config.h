#pragma once

#include "data/particle_mask_config.h"

struct InputFilterConfig {
  bool enabled = true;
  ParticleMaskConfig mask;
};
