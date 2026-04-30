#pragma once

#include "render/render_resources.h"

class ParticleArray;
struct AppDerivedState;
struct ParticleVisualConfig;
struct ProjectionPreviewDerivedState;
struct RenderRuntimeState;
struct RenderSystem;
struct CameraContext;

struct ParticleRenderBuildResult {
  bool particlesBuilt = false;
  bool velocityBuilt = false;
};

ParticleRenderInput MakeParticleRenderInput(const ParticleArray& particles);

ParticleRenderBuildResult UpdateRenderSceneData(const ParticleRenderInput& particleInput,
                                                const ParticleVisualConfig& particleVisual,
                                                const CameraContext& camera,
                                                double currentTime,
                                                bool softwareRenderer,
                                                RenderRuntimeState& render,
                                                const AppDerivedState& derived,
                                                RenderSystem& rs);

void AcknowledgeParticleRenderBuild(ParticleArray& particles,
                                    const ParticleRenderBuildResult& result);

void UpdateProjectionPreviewTexture(ProjectionPreviewDerivedState& source,
                                    RenderSystem& render);
