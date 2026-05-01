#pragma once

class ParticleArray;
struct CameraContext;
struct ViewFilterConfig;

void ApplyCullingSphere(ParticleArray& particles,
                        const ViewFilterConfig& viewFilter);

void ClearVisibilityMask(ParticleArray& particles);
