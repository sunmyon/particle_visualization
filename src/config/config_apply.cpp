#include "config_apply.h"

#include <unordered_set>

#include "interaction/camera.h"
#include "make_2D_projection_map.h"
#include "FileIO/file_io.h"

void ApplyConfig(const AppConfig& cfg,
                 ParticleArray& P,
                 CameraContext& camCtx,
                 FileInfo& fileInfo,
                 ProjectionMapGenerator* projection)
{
  // file
  fileInfo.currentFilePath = cfg.file.currentPath; // 実メンバ名に合わせて修正

  // camera
  camCtx.cameraPos    = glm::vec3(cfg.camera.cameraPos[0],    cfg.camera.cameraPos[1],    cfg.camera.cameraPos[2]);
  camCtx.cameraTarget = glm::vec3(cfg.camera.cameraTarget[0], cfg.camera.cameraTarget[1], cfg.camera.cameraTarget[2]);
  camCtx.cameraUp     = glm::vec3(cfg.camera.cameraUp[0],     cfg.camera.cameraUp[1],     cfg.camera.cameraUp[2]);

  // follow
  P.flag_follow_particle_ID = cfg.follow.followParticleID;
  P.TargetParticleID = cfg.follow.targetParticleID;
  P.flag_follow_clump_center = cfg.follow.followClumpCenter;

  // projection
  if (projection && cfg.projection.enabled) {
    projection->params.npixel = cfg.projection.npixel;
    for (int k = 0; k < 3; ++k) {
      projection->params.xlen[k] = cfg.projection.xlen[k];
      projection->params.xoffset[k] = cfg.projection.xoffset[k];
      projection->params.tilt[k] = cfg.projection.tilt[k];
    }
    projection->params.selectedAxis = cfg.projection.selectedAxis;
    projection->params.selectedType = cfg.projection.selectedType;
    projection->params.flagLogScale = cfg.projection.flagLogScale;
    projection->params.autoRange = cfg.projection.autoRange;
    projection->params.range_min = cfg.projection.rangeMin;
    projection->params.range_max = cfg.projection.rangeMax;
    projection->params.flagDensityWeight = cfg.projection.flagDensityWeight;
    projection->params.flagVoronoi = cfg.projection.flagVoronoi;
    projection->params.step_z = cfg.projection.stepZ;
  }

  // explicit mask
  if (cfg.mask.saveExplicitMask) {
    std::unordered_set<int> maskedIDs(cfg.mask.explicitMaskedParticleIDs.begin(),
                                      cfg.mask.explicitMaskedParticleIDs.end());

    if (P.flag_mask.size() != P.particleBlock.particles.size()) {
      P.flag_mask.resize(P.particleBlock.particles.size(), 0);
    }

    for (size_t i = 0; i < P.particleBlock.particles.size(); ++i) {
      int id = P.particleBlock.particles[i].ID;
      P.flag_mask[i] = (maskedIDs.count(id) > 0) ? 1 : 0;
    }
  }
}
