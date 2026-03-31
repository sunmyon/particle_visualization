#pragma once

#include <glad/glad.h>

class ParticleArray;

struct RenderResources {
#ifdef VOLUME_RENDERING
  GLuint fullscreenVAO = 0;
#endif
};

bool InitRenderResources(RenderResources& resources, ParticleArray& particles);
void DestroyRenderResources(RenderResources& resources);
