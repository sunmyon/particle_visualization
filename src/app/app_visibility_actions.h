#pragma once

class ParticleArray;
struct CameraContext;
struct ViewFilterConfig;
struct NormalizationContext;

void ApplyCullingSphere(ParticleArray& particles,
			const NormalizationContext& normalization,
                        const ViewFilterConfig& viewFilter);

void ClearVisibilityMask(ParticleArray& particles);
