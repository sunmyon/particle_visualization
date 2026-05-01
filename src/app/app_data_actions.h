#pragma once

class SimulationDataset;
struct NormalizationContext;

void NormalizeParticlePositions(SimulationDataset& particles,
                                NormalizationContext& normalization);
