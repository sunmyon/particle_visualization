#pragma once

struct ConfigData;
class FileInfo;
class ParticleArray;
struct ParticleVisualConfig;
struct ParticleMaskConfig;

void ApplyConfigData(const ConfigData& config,
                     FileInfo& fileInfo,
		     UnitSystem& units,
		     float& desired_max,
                     ParticleVisualConfig& visual,
                     ParticleMaskConfig& mask);
