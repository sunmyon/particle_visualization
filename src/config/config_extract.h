#pragma once

struct ConfigData;
class FileInfo;
class ParticleArray;
struct ParticleVisualConfig;
struct ParticleMaskConfig;
struct UnitSystem;

ConfigData ExtractConfigData(const FileInfo& fileInfo,
                             const UnitSystem& units,
			     const float desired_max,
                             const ParticleVisualConfig& visual,
                             const ParticleMaskConfig& mask);
