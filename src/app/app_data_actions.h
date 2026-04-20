#pragma once

class ParticleArray;
struct NormalizationContext;

void NormalizeParticlePositions(ParticleArray& particles,
                                NormalizationContext& normalization);
