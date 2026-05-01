#pragma once

class SimulationDataset;
struct CameraContext;
struct ViewFilterConfig;

void ApplyCullingSphere(SimulationDataset& particles,
                        const ViewFilterConfig& viewFilter);

void ClearVisibilityMask(SimulationDataset& particles);
