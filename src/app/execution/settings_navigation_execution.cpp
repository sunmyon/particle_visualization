#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cmath>
#include <cstdlib>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "app/execution/analysis_execution.h"
#include "app/state/app_state.h"
#include "app/state/analysis_state.h"
#include "app/state/runtime_state.h"
#include "app/state/normalization_config.h"
#include "app/state/tracking_view_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/execution/snapshot_sequence_job.h"
#include "app/app_visibility_actions.h"
#include "app/app_data_actions.h"
#include "data/particle_array.h"
#include "data/particle_selection.h"
#include "data/clump_loader.h"
#include "data/clump_store.h"
#include "data/halo_store.h"
#include "render/scene_objects.h"
void ExecuteFileNavigationRequests(FileNavigationRuntimeState& rt,
                                   SnapshotLoadRuntimeState& snapshotLoad)
{
  auto& req = rt.request;
  auto& nav = rt.navigation;

  auto enqueueUserNavLoad = [&](int step) {
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        step,
                        100); // Prioritize user operations.
  };

  if (req.applySkipStepRequested) {
    if (rt.tempSkipStep > 0) {
      nav.currentStep = std::max(
        0,
        static_cast<int>(std::round(
          (nav.currentFileIndex - nav.initialIndex) /
          static_cast<float>(rt.tempSkipStep)))
      );
      nav.skipStep = rt.tempSkipStep;
      RecomputeCurrentFileIndex(rt);
      enqueueUserNavLoad(nav.currentStep);
    }
    req.applySkipStepRequested = false;
  }

  if (req.loadSelectedSnapshotRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadSelectedSnapshotRequested = false;
  }

  if (req.loadPreviousRequested) {
    if (nav.currentStep > 0) {
      --nav.currentStep;
    }
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadPreviousRequested = false;
  }

  if (req.loadNextRequested) {
    ++nav.currentStep;
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadNextRequested = false;
  }

  if (req.loadBatchRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.loadBatchRequested = false;
  }

  if (req.reloadRequested) {
    RecomputeCurrentFileIndex(rt);
    enqueueUserNavLoad(nav.currentStep);
    req.reloadRequested = false;
  }

  if (req.generateTestDataRequested) {
    RecomputeCurrentFileIndex(rt);
    RequestSnapshotLoad(snapshotLoad,
                        SnapshotLoadOwner::UserNavigation,
                        nav.currentStep,
                        1,
                        SnapshotLoadKind::GenerateTestData);
    req.generateTestDataRequested = false;
  }
}

void ExecuteSettingsActionRequests(ParticleArray& particles,
                                   QuantityState& quantity,
                                   ParticleVisualConfig& particleVisual,
                                   RenderRuntimeState& render,
                                   SettingsRuntimeState& settings,
                                   SnapshotPostprocessState& post)
{
  auto& req = settings.request;

  if (req.applyParticleVisualRequested) {
    particleVisual = req.particleVisualDraft;
    particles.particlesDirty = true;
    req.applyParticleVisualRequested = false;
    req.particleVisualDraftDirty = false;
    req.particleRenderDirtyRequested = false;
  }

  if (req.applyRenderRequested) {
    render.particleLabels = req.renderDraft.particleLabels;
    render.velocity = req.renderDraft.velocity;
    render.disks.opacity = req.renderDraft.diskOpacity;
    render.ellipsoids.opacity = req.renderDraft.ellipsoidOpacity;
    render.isocontour.opacity = req.renderDraft.isoContourOpacity;
    render.crossGizmo.size = req.renderDraft.crossGizmoSize;
    req.applyRenderRequested = false;
    req.renderDraftDirty = false;
  }

  if (req.applyUnitsRequested) {
    quantity.units = req.unitsDraft;
    quantity.units.updateDerived();
    req.unitConversionRebuildRequested = true;
    req.applyUnitsRequested = false;
    req.unitsDraftDirty = false;
  }

  if (req.normalizeRequested) {
    settings.normalization.originalMax = quantity.range.originalMax;
    NormalizeParticlePositions(particles, settings.normalization);
    post.refreshTree = true;
    post.refreshCulling = true;
    post.refreshTopParticles = true;
    req.normalizeRequested = false;
  }

  if (req.particleRenderDirtyRequested) {
    particles.particlesDirty = true;
    req.particleRenderDirtyRequested = false;
  }

  if (req.velocityRenderDirtyRequested) {
    particles.velocityDirty = true;
    req.velocityRenderDirtyRequested = false;
  }

  if (req.unitConversionRebuildRequested) {
    auto& current = settings.fileNavigation.current;
    current.useComovingCoordinates = quantity.units.useComovingCoordinate;
    if (current.useComovingCoordinates) {
      double a = current.loadedTime;
      if (!std::isfinite(a) || a <= 0.0) {
        a = 1.0;
      }
      current.loadedScaleFactor = a;
      current.loadedRedshift = (1.0 / a) - 1.0;
    } else {
      current.loadedScaleFactor = 1.0;
      current.loadedRedshift = 0.0;
    }

    quantity.conversion.displaySpace =
      quantity.units.useComovingCoordinate
      ? UnitSpace::Comoving
      : UnitSpace::Physical;
    quantity.rebuildConversion(settings.fileNavigation.current.loadedScaleFactor);
    req.unitConversionRebuildRequested = false;
  }
}

void ExecuteCameraPlacementRequests(ParticleArray& particles,
				    const NormalizationContext& normalization,
				    ViewFilterConfig& viewFilter,
				    CameraContext& camCtx,
				    SettingsRuntimeState& rt,
				    SnapshotPostprocessState &post)
{
  auto& req = rt.cameraPlacement;

  if (req.setCenterRequested) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

    if (req.inputIsOriginal) {
      float scale = normalization.toPhysicalScale();
      camCtx.cameraTarget =
        glm::vec3(req.centerInput[0], req.centerInput[1], req.centerInput[2]) * scale;
    } else {
      camCtx.cameraTarget =
        glm::vec3(req.centerInput[0], req.centerInput[1], req.centerInput[2]);
    }

    camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    req.setCenterRequested = false;
  }

  if (req.setProjectionRequested) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);

    switch (req.currentView) {
    case 0:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(distance, 0.0f, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 1:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(-distance, 0.0f, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 2:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, distance, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 0.0f, -1.0f);
      break;
    case 3:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, -distance, 0.0f);
      camCtx.cameraUp  = glm::vec3(0.0f, 0.0f, 1.0f);
      break;
    case 4:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, distance);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 5:
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, -distance);
      camCtx.cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    }

    glm::vec3 viewDir = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
    glm::quat rollQuat = glm::angleAxis(glm::radians(req.rollAngle), viewDir);
    camCtx.cameraUp = rollQuat * camCtx.cameraUp;

    glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
    camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));

    req.setProjectionRequested = false;
  }

  if (req.applyCullingRequested) {
    ApplyCullingSphere(particles, normalization, viewFilter);
    viewFilter.enabled = true;
    req.applyCullingRequested = false;
  }

  if (req.clearCullingRequested) {
    ClearVisibilityMask(particles);
    viewFilter.enabled = false;
    req.clearCullingRequested = false;
  }

  if(post.refreshCulling){
    if(viewFilter.enabled)
      ApplyCullingSphere(particles, normalization, viewFilter);
    post.refreshCulling = false;
  }
}

static void RecenterCameraKeepView(CameraContext& camCtx, const glm::vec3& target)
{
  float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
  if (dist < 1e-6f) {
    dist = (camCtx.distance > 1e-6f) ? camCtx.distance : 1.0f;
  }

  glm::vec3 dir = camCtx.cameraTarget - camCtx.cameraPos;
  if (glm::dot(dir, dir) < 1e-12f) {
    dir = glm::vec3(0.0f, 0.0f, -1.0f);
  } else {
    dir = glm::normalize(dir);
  }

  camCtx.cameraTarget = target;
  camCtx.cameraPos = target - dir * dist;
  camCtx.distance = dist;

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
}


static glm::vec3 StabilizeAxisSign(glm::vec3 axis, TrackingTargetState& track)
{
  if (!track.amKeepSignContinuity) return axis;

  if (track.amHasLastAxis) {
    glm::vec3 last(track.amLastAxis[0], track.amLastAxis[1], track.amLastAxis[2]);
    if (glm::dot(axis, last) < 0.0f) {
      axis = -axis;
    }
  }

  track.amLastAxis[0] = axis.x;
  track.amLastAxis[1] = axis.y;
  track.amLastAxis[2] = axis.z;
  track.amHasLastAxis = true;

  return axis;
}

static void ApplyCameraAlignmentFromAxis(CameraContext& camCtx,
                                         const glm::vec3& center,
                                         const glm::vec3& axisIn,
                                         AngularMomentumViewMode mode)
{
  glm::vec3 axis = glm::normalize(axisIn);

  float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
  if (dist < 1e-6f) {
    dist = (camCtx.distance > 1e-6f) ? camCtx.distance : 1.0f;
  }

  glm::vec3 prevF = camCtx.cameraTarget - camCtx.cameraPos;
  if (glm::dot(prevF, prevF) < 1e-12f) prevF = glm::vec3(0.0f, 0.0f, -1.0f);
  prevF = glm::normalize(prevF);

  glm::vec3 forward(0.0f);

  if (mode == AngularMomentumViewMode::FaceOn) {
    glm::vec3 f1 = -axis;
    glm::vec3 f2 =  axis;
    forward = (glm::dot(f1, prevF) >= glm::dot(f2, prevF)) ? f1 : f2;
  } else {
    glm::vec3 proj = prevF - glm::dot(prevF, axis) * axis;
    if (glm::dot(proj, proj) < 1e-12f) {
      glm::vec3 c1 = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
      if (glm::dot(c1, c1) < 1e-12f) {
        c1 = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
      }
      c1 = glm::normalize(c1);
      glm::vec3 c2 = -c1;
      forward = (glm::dot(c1, prevF) >= glm::dot(c2, prevF)) ? c1 : c2;
    } else {
      forward = glm::normalize(proj);
    }
  }

  glm::vec3 upHint = (mode == AngularMomentumViewMode::EdgeOn) ? axis : camCtx.cameraUp;
  if (glm::dot(upHint, upHint) < 1e-12f) upHint = glm::vec3(0.0f, 1.0f, 0.0f);
  upHint = glm::normalize(upHint);

  if (std::abs(glm::dot(upHint, forward)) > 0.95f) {
    upHint = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(upHint, forward)) > 0.95f) {
      upHint = glm::vec3(1.0f, 0.0f, 0.0f);
    }
  }

  glm::vec3 right = glm::cross(forward, upHint);
  if (glm::dot(right, right) < 1e-12f) {
    upHint = glm::vec3(1.0f, 0.0f, 0.0f);
    right = glm::cross(forward, upHint);
  }
  right = glm::normalize(right);

  glm::vec3 up = glm::normalize(glm::cross(right, forward));

  camCtx.cameraTarget = center;
  camCtx.cameraPos = center - forward * dist;
  camCtx.cameraUp = up;
  camCtx.distance = dist;

  glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
  camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
}

static bool ResolveTrackingCenter(ParticleArray& particles,
                                  ClumpStore& clumpStore,
                                  const NormalizationContext& normalization,
                                  TrackingTargetState& track,
                                  int currentFileIndex,
                                  glm::vec3& outCenter)
{
#ifdef CLUMP_DATA_READ
  if (track.followClump) {
    if (clumpStore.filePath().empty() || clumpStore.empty()) {
      track.followClump = false;
    } else {
      int idx = clumpStore.findIndexByClumpID(track.targetClumpID);
      if (idx < 0 && track.targetClumpID >= 0 && track.targetClumpID < static_cast<int>(clumpStore.size())) {
        idx = track.targetClumpID;
      }

      if (idx < 0 || idx >= static_cast<int>(clumpStore.size())) {
        track.followClump = false;
      } else {
        ClumpData targetClump = clumpStore.clump(idx);

        TrackingVector<ClumpData> newClumps =
          loadClumpData(clumpStore.filePath().c_str(),
                        currentFileIndex,
                        normalization.toNormalizedScale());

        if (newClumps.empty()) {
          track.followClump = false;
        } else {
          clumpStore.setClumps(std::move(newClumps));

          float nextPos[3] = {0.f, 0.f, 0.f};
          targetClump.get_next_clump_position(clumpStore.clumps(), nextPos);
          outCenter = glm::vec3(nextPos[0], nextPos[1], nextPos[2]);
          return true;
        }
      }
    }
  }
#endif

  if (track.followParticle) {
    float pos[3] = {0.f, 0.f, 0.f};
    if (!particles.findParticleID(track.targetParticleID, pos)) {
      track.followParticle = false;
    } else {
      outCenter = glm::vec3(pos[0], pos[1], pos[2]);
      return true;
    }
  }

  if (track.followSinkParticle) {
    float targetPos[3] = {0.f, 0.f, 0.f};
    bool found = false;

    if (!track.followSinkParticleMostMassive) {
      found = particles.findParticleID(track.targetSinkParticleID, targetPos);
    }

    if (track.followSinkParticleMostMassive || !found) {
      double massMax = -1.0;
      for (const auto& p : particles.particleBlock.particles) {
        if (p.type < 3) continue;
        if (p.mass > massMax) {
          targetPos[0] = p.pos[0];
          targetPos[1] = p.pos[1];
          targetPos[2] = p.pos[2];
          massMax = p.mass;
          found = true;
        }
      }
    }

    if (!found) {
      track.followSinkParticle = false;
    } else {
      if (track.useMassCenter) {
        double dPos[3] = {0.0, 0.0, 0.0};
        double weight = 0.0;
        const double r2max = (track.massCenterRadius > 0.0f)
                           ? static_cast<double>(track.massCenterRadius) * static_cast<double>(track.massCenterRadius)
                           : -1.0;

        for (const auto& p : particles.particleBlock.particles) {
          if (p.type == 1 || p.type == 2) continue;
          if (p.type == 0 && p.density < track.massCenterMinDensity) continue;

          const double dx = static_cast<double>(targetPos[0]) - static_cast<double>(p.pos[0]);
          const double dy = static_cast<double>(targetPos[1]) - static_cast<double>(p.pos[1]);
          const double dz = static_cast<double>(targetPos[2]) - static_cast<double>(p.pos[2]);
          const double dist2 = dx * dx + dy * dy + dz * dz;
          if (r2max > 0.0 && dist2 > r2max) continue;

          dPos[0] += static_cast<double>(p.mass) * dx;
          dPos[1] += static_cast<double>(p.mass) * dy;
          dPos[2] += static_cast<double>(p.mass) * dz;
          weight += static_cast<double>(p.mass);
        }

        if (weight > 0.0) {
          targetPos[0] += static_cast<float>(dPos[0] / weight);
          targetPos[1] += static_cast<float>(dPos[1] / weight);
          targetPos[2] += static_cast<float>(dPos[2] / weight);
        }
      }

      outCenter = glm::vec3(targetPos[0], targetPos[1], targetPos[2]);
      return true;
    }
  }

  return false;
}


void ExecutePostSnapshotLoadActions(ParticleArray& particles,
                                    ClumpStore& clumpStore,
                                    NormalizationContext& normalization,
                                    TrackingTargetState& track,
                                    CameraContext& camCtx,
                                    SnapshotPostprocessState& post,
                                    int currentFileIndex)
{
  if (!post.applyTrackingToCamera && !track.renewAfterSnapshot) {
    return;
  }

  glm::vec3 center = camCtx.cameraTarget;
  const bool foundCenter =
    ResolveTrackingCenter(particles,
                          clumpStore,
                          normalization,
                          track,
                          currentFileIndex,
                          center);

  if (foundCenter) {
    if (track.alignToAngularMomentum) {
      ParticleSelectionOption op;
      op.center = center;
      op.radius = track.amRadius;
      op.useType = track.amUseType;
      op.flagSubtractBulkVelocity = track.amSubtractBulkVelocity;
      
      glm::vec3 axis(0.0f, 0.0f, 1.0f);
      if (particles.particleBlock.ComputeAngularMomentumAxis(op, axis)) {
        axis = StabilizeAxisSign(axis, track);
        ApplyCameraAlignmentFromAxis(camCtx, center, axis, track.amViewMode);
      } else {
        RecenterCameraKeepView(camCtx, center);
      }
    } else {
      RecenterCameraKeepView(camCtx, center);
    }
  }

  post.applyTrackingToCamera = false;
  track.renewAfterSnapshot = false;
}
