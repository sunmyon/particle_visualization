#pragma once

#include <string>
#include <vector>
#include "app/ui_state.h"

class ParticleArray;
class FileInfo;
struct ParticleVisualConfig;

bool loadConfig(const std::string& filename,
                ParticleArray* P,
                FileInfo* fileInfo,
                ParticleVisualConfig* visualCfg,
                MaskUIState* outMaskState = nullptr);

bool saveConfig(const std::string& filename,
                const ParticleArray* P,
                const FileInfo* fileInfo,
                const ParticleVisualConfig* visualCfg,
                const MaskUIState* maskState = nullptr);
