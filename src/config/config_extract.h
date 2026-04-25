#pragma once

struct ConfigData;
struct FileNavigationRuntimeState;
struct SnapshotFormatState;
struct ParticleVisualConfig;
struct ParticleMaskConfig;
struct UnitSystem;

ConfigData ExtractConfigData(const FileNavigationRuntimeState& fileNav,
                             const SnapshotFormatState& format,
                             const UnitSystem& units,
			     const float desired_max,
                             const ParticleVisualConfig& visual,
                             const ParticleMaskConfig& mask);
