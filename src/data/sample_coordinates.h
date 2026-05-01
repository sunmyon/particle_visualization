#pragma once

#include <glm/vec3.hpp>

#include "data/simulation_element.h"

inline glm::vec3 renderPosition(const SimulationElement& p,
                                float worldToRenderScale)
{
  return glm::vec3(p.position[0] * worldToRenderScale,
                   p.position[1] * worldToRenderScale,
                   p.position[2] * worldToRenderScale);
}

inline void renderPosition(const SimulationElement& p,
                           float worldToRenderScale,
                           float out[3])
{
  out[0] = p.position[0] * worldToRenderScale;
  out[1] = p.position[1] * worldToRenderScale;
  out[2] = p.position[2] * worldToRenderScale;
}

inline float renderSupportRadius(const SimulationElement& p,
                                 float worldToRenderScale)
{
  return p.supportRadius * worldToRenderScale;
}
