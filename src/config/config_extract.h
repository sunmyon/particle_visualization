#pragma once

#include "app_config.h"

struct ParticleArray;
struct CameraContext;
class ProjectionMapGenerator;

// gFileInfo の実型名に合わせて直してください
class FileInfo;

AppConfig ExtractConfig(const ParticleArray& P,
                        const CameraContext& camCtx,
                        const FileInfo& fileInfo,
                        const ProjectionMapGenerator* projection = nullptr);
