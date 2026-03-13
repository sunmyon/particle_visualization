#pragma once

#include "app_config.h"

struct ParticleArray;
struct CameraContext;
class ProjectionMapGenerator;

// gFileInfo の実型名に合わせて直してください
class FileInfo;

void ApplyConfig(const AppConfig& cfg,
                 ParticleArray& P,
                 CameraContext& camCtx,
                 FileInfo& fileInfo,
                 ProjectionMapGenerator* projection = nullptr);
