#pragma once

struct NormalizationContext {
  float desiredMax = 1.0f;
  float originalMax = 0.0f;

  float toNormalizedScale() const {
    if (originalMax <= 0.0f) return 1.0f;
    return desiredMax / originalMax;
  }

  float toPhysicalScale() const {
    if (desiredMax <= 0.0f) return 1.0f;
    return originalMax / desiredMax;
  }
};
