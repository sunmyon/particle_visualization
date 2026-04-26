#pragma once

#include "render/render_resources.h"

class ParticleArray;
struct AppDerivedState;
struct ParticleVisualConfig;
struct ProjectionPreviewDerivedState;
struct RenderRuntimeState;
struct RenderSystem;

struct ParticleRenderUploadResult {
  bool particlesUploaded = false;
  bool velocityUploaded = false;
};

ParticleRenderInput MakeParticleRenderInput(const ParticleArray& particles);

ParticleRenderUploadResult UpdateRenderResources(const ParticleRenderInput& particleInput,
                                                 const ParticleVisualConfig& particleVisual,
                                                 RenderRuntimeState& render,
                                                 const AppDerivedState& derived,
                                                 RenderSystem& rs);

void AcknowledgeParticleRenderUploads(ParticleArray& particles,
                                      const ParticleRenderUploadResult& result);

void UpdateProjectionPreviewTexture(ProjectionPreviewDerivedState& source,
                                    RenderSystem& render);
