#pragma once

struct ConfigData;
struct FileNavigationRuntimeState;
struct SnapshotFormatState;
struct UnitSystem;
struct ParticleVisualConfig;
struct ParticleMaskConfig;

void ApplyConfigData(const ConfigData& config,
                     FileNavigationRuntimeState& fileNav,
                     SnapshotFormatState& format,
		     UnitSystem& units,
		     float& desired_max,
                     ParticleVisualConfig& visual,
                     ParticleMaskConfig& mask);
