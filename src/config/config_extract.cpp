#include "config_extract.h"

#include "main.h"
#include "camera.h"
#include "make_2D_projection_map.h"
#include "FileIO/file_io.h"   // 実際の型名に合わせて修正

AppConfig ExtractConfig(const ParticleArray& P,
                        const CameraContext& camCtx,
                        const FileInfo& fileInfo,
                        const ProjectionMapGenerator* projection)
{
  AppConfig cfg;

  // file
  cfg.file.currentPath = fileInfo.currentFilePath;  // 実際のメンバ名に合わせて修正
  cfg.file.currentHaloCatalogPath = "";

  // camera
  cfg.camera.cameraPos    = {camCtx.cameraPos.x,    camCtx.cameraPos.y,    camCtx.cameraPos.z};
  cfg.camera.cameraTarget = {camCtx.cameraTarget.x, camCtx.cameraTarget.y, camCtx.cameraTarget.z};
  cfg.camera.cameraUp     = {camCtx.cameraUp.x,     camCtx.cameraUp.y,     camCtx.cameraUp.z};

  // follow
  cfg.follow.followParticleID = P.flag_follow_particle_ID;
  cfg.follow.targetParticleID = P.TargetParticleID;
  cfg.follow.followClumpCenter = P.flag_follow_clump_center;

  // display
  for (int i = 0; i < 6; ++i) {
    cfg.display.drawType[i] = true; // ここは実際の type visibility state に合わせて接続
    cfg.mask.typeEnabled[i] = true; // 同上
  }

  // projection
  if (projection) {
    cfg.projection.enabled = true;
    cfg.projection.npixel = projection->params.npixel;
    for (int k = 0; k < 3; ++k) {
      cfg.projection.xlen[k] = projection->params.xlen[k];
      cfg.projection.xoffset[k] = projection->params.xoffset[k];
      cfg.projection.tilt[k] = projection->params.tilt[k];
    }
    cfg.projection.selectedAxis = projection->params.selectedAxis;
    cfg.projection.selectedType = projection->params.selectedType;
    cfg.projection.flagLogScale = projection->params.flagLogScale;
    cfg.projection.autoRange = projection->params.autoRange;
    cfg.projection.rangeMin = projection->params.range_min;
    cfg.projection.rangeMax = projection->params.range_max;
    cfg.projection.flagDensityWeight = projection->params.flagDensityWeight;
    cfg.projection.flagVoronoi = projection->params.flagVoronoi;
    cfg.projection.stepZ = projection->params.step_z;
  }

  // mask
  if (cfg.mask.saveExplicitMask) {
    cfg.mask.explicitMaskedParticleIDs.clear();
    for (size_t i = 0; i < P.particleBlock.particles.size(); ++i) {
      if (i < P.flag_mask.size() && P.flag_mask[i] != 0) {
        cfg.mask.explicitMaskedParticleIDs.push_back(P.particleBlock.particles[i].ID);
      }
    }
  }

  return cfg;
}
