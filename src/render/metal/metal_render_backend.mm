#include "render/render_backend.h"

#include "platform/metal_context.h"
#include "image/rgb_image.h"
#include "projection/projection_map_ui_state.h"
#include "render/colormap_defs.h"
#include "render/particle_visual_config.h"
#include "render/render_resources.h"
#include "render/render_system.h"

#import <Metal/Metal.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <chrono>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace {

bool ShouldUseParticleLod(const RenderRuntimeState& render,
                          const std::vector<RenderParticle>& proxy,
                          bool softwareRenderer,
                          bool allowEmptyProxy = false)
{
  if (proxy.empty() && !allowEmptyProxy) {
    return false;
  }

  switch (render.scheduling.particleLod.mode) {
    case ParticleLodMode::Off:
      return render.scheduling.autoParticleLodOnSoftwareRenderer &&
             softwareRenderer &&
             render.scheduling.interactionActive;
    case ParticleLodMode::WhileInteracting:
      return render.scheduling.interactionActive;
    case ParticleLodMode::Always:
      return true;
  }
  return false;
}

bool EnvFlagDisabled(const char* name)
{
  const char* value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "0") == 0;
}

bool EnvFlagEnabled(const char* name)
{
  const char* value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "0") != 0;
}

constexpr const char* kParticleShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct RenderParticle {
  packed_float3 pos;
  uchar type;
  uchar flagStress;
  ushort pad;
  float hsml;
  float valShow;
};

struct LineVertex {
  float4 pos;
  float4 color;
};

struct SolidVertex {
  float4 pos;
};

struct SolidInstance {
  float4 model0;
  float4 model1;
  float4 model2;
  float4 model3;
  float4 colorOpacity;
};

struct ParticleUniforms {
  float4x4 mvp;
  float4 typeParamsA[6]; // pointSize, valueMin, valueMax, useLog.
  float4 typeParamsB[6]; // periodic, hidden, colormapIndex, pad.
  float4 typeColors[6];
  float4 misc; // pointScale, globalAlpha, colormapCount, fixedLodDebugColor.
};

struct ParticleRangeUniforms {
  uint maxLeafCount;
};

struct LineUniforms {
  float4x4 mvp;
  float opacityScale;
};

struct SolidUniforms {
  float4x4 viewProjection;
  float opacityScale;
};

struct ParticleVertexOut {
  float4 position [[position]];
  float pointSize [[point_size]];
  float valShow;
  uint type;
};

vertex ParticleVertexOut particleVertex(uint vertexId [[vertex_id]],
                                        constant RenderParticle* particles [[buffer(0)]],
                                        constant ParticleUniforms& uniforms [[buffer(1)]])
{
  RenderParticle p = particles[vertexId];
  uint type = min(uint(p.type), 5u);
  float4 paramsA = uniforms.typeParamsA[type];

  ParticleVertexOut out;
  out.position = uniforms.mvp * float4(float3(p.pos), 1.0);
  out.pointSize = max(paramsA.x * uniforms.misc.x, 1.0);
  out.valShow = p.valShow;
  out.type = type;
  return out;
}

vertex ParticleVertexOut particleIndexedVertex(
  uint vertexId [[vertex_id]],
  constant RenderParticle* particles [[buffer(0)]],
  constant ParticleUniforms& uniforms [[buffer(1)]],
  constant uint* indices [[buffer(2)]])
{
  RenderParticle p = particles[indices[vertexId]];
  uint type = min(uint(p.type), 5u);
  float4 paramsA = uniforms.typeParamsA[type];

  ParticleVertexOut out;
  out.position = uniforms.mvp * float4(float3(p.pos), 1.0);
  out.pointSize = max(paramsA.x * uniforms.misc.x, 1.0);
  out.valShow = p.valShow;
  out.type = type;
  return out;
}

vertex ParticleVertexOut particleRangeVertex(
  uint vertexId [[vertex_id]],
  constant RenderParticle* particles [[buffer(0)]],
  constant ParticleUniforms& uniforms [[buffer(1)]],
  constant uint4* leafRanges [[buffer(2)]],
  constant ParticleRangeUniforms& rangeUniforms [[buffer(3)]])
{
  const uint maxLeafCount = max(rangeUniforms.maxLeafCount, 1u);
  const uint rangeIndex = vertexId / maxLeafCount;
  const uint localIndex = vertexId - rangeIndex * maxLeafCount;
  const uint4 range = leafRanges[rangeIndex];

  ParticleVertexOut out;
  if (localIndex >= range.y) {
    out.position = float4(2.0, 2.0, 0.0, 1.0);
    out.pointSize = 0.0;
    out.valShow = 0.0;
    out.type = 0u;
    return out;
  }

  RenderParticle p = particles[range.x + localIndex];
  uint type = min(uint(p.type), 5u);
  float4 paramsA = uniforms.typeParamsA[type];
  out.position = uniforms.mvp * float4(float3(p.pos), 1.0);
  out.pointSize = max(paramsA.x * uniforms.misc.x, 1.0);
  out.valShow = p.valShow;
  out.type = type;
  return out;
}

fragment float4 particleFragment(ParticleVertexOut in [[stage_in]],
                                 float2 pointCoord [[point_coord]],
                                 constant ParticleUniforms& uniforms [[buffer(1)]],
                                 texture2d<float> colormapTexture [[texture(0)]],
                                 sampler colormapSampler [[sampler(0)]])
{
  float2 uv = pointCoord * 2.0 - 1.0;
  float r2 = dot(uv, uv);
  if (r2 > 1.0) {
    discard_fragment();
  }

  uint type = min(in.type, 5u);
  float4 paramsA = uniforms.typeParamsA[type];
  float4 paramsB = uniforms.typeParamsB[type];
  if (paramsB.y > 0.5) {
    discard_fragment();
  }
  float minV = paramsA.y;
  float maxV = paramsA.z;
  float v = in.valShow;
  if (paramsA.w > 0.5) {
    v = log10(max(v, 1.0e-30));
  }

  float normV = (maxV > minV) ? (v - minV) / (maxV - minV) : 0.0;
  if (paramsB.x > 0.5) {
    normV = fract(normV);
  } else {
    normV = clamp(normV, 0.0, 1.0);
  }

  if (uniforms.misc.w > 0.5) {
    return float4(1.0, 0.0, 1.0, 1.0);
  }
  float4 base = uniforms.typeColors[type];
  float cmapCount = max(uniforms.misc.z, 1.0);
  float cmapRow = clamp(paramsB.z, 0.0, cmapCount - 1.0);
  float vTex = (cmapRow + 0.5) / cmapCount;
  float3 color = colormapTexture.sample(colormapSampler,
                                        float2(normV, vTex)).rgb;
  float edge = smoothstep(1.0, 0.82, r2);
  return float4(color, edge * uniforms.misc.y);
}

struct LineVertexOut {
  float4 position [[position]];
  float4 color;
};

vertex LineVertexOut lineVertex(uint vertexId [[vertex_id]],
                                constant LineVertex* vertices [[buffer(0)]],
                                constant LineUniforms& uniforms [[buffer(1)]])
{
  LineVertex v = vertices[vertexId];
  LineVertexOut out;
  out.position = uniforms.mvp * float4(v.pos.xyz, 1.0);
  out.color = float4(v.color.rgb, v.color.a * uniforms.opacityScale);
  return out;
}

fragment float4 lineFragment(LineVertexOut in [[stage_in]])
{
  return in.color;
}

struct SolidVertexOut {
  float4 position [[position]];
  float4 color;
};

vertex SolidVertexOut solidVertex(uint vertexId [[vertex_id]],
                                  uint instanceId [[instance_id]],
                                  constant SolidVertex* vertices [[buffer(0)]],
                                  constant SolidInstance* instances [[buffer(1)]],
                                  constant SolidUniforms& uniforms [[buffer(2)]])
{
  SolidVertex v = vertices[vertexId];
  SolidInstance inst = instances[instanceId];
  float4x4 model =
    float4x4(inst.model0, inst.model1, inst.model2, inst.model3);

  SolidVertexOut out;
  out.position = uniforms.viewProjection * model * float4(v.pos.xyz, 1.0);
  out.color = float4(inst.colorOpacity.rgb,
                     inst.colorOpacity.a * uniforms.opacityScale);
  return out;
}

fragment float4 solidFragment(SolidVertexOut in [[stage_in]])
{
  return in.color;
}

struct CacheVertexOut {
  float4 position [[position]];
  float2 uv;
};

vertex CacheVertexOut cacheVertex(uint vertexId [[vertex_id]])
{
  float2 pos[3] = {
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0)
  };
  CacheVertexOut out;
  out.position = float4(pos[vertexId], 0.0, 1.0);
  float2 uv = pos[vertexId] * 0.5 + 0.5;
  out.uv = float2(uv.x, 1.0 - uv.y);
  return out;
}

struct CacheFragmentOut {
  float4 color [[color(0)]];
  float depth [[depth(any)]];
};

fragment float4 colorCacheFragment(CacheVertexOut in [[stage_in]],
                                   texture2d<float> colorTexture [[texture(0)]],
                                   sampler colorSampler [[sampler(0)]])
{
  return colorTexture.sample(colorSampler, in.uv);
}

fragment CacheFragmentOut particleCacheFragment(
  CacheVertexOut in [[stage_in]],
  texture2d<float> colorTexture [[texture(0)]],
  depth2d<float> depthTexture [[texture(1)]],
  sampler colorSampler [[sampler(0)]])
{
  CacheFragmentOut out;
  out.color = colorTexture.sample(colorSampler, in.uv);
  out.depth = depthTexture.sample(colorSampler, in.uv);
  return out;
}

struct ParticleLodComputeUniforms {
  float4 cameraPosFocalPx;
  float4 focusPosProtectRadius;
  float4 lodParams;
  float4 frustumPlanes[6];
  uint nodeCount;
  uint maxOutput;
  uint maxLeafOutput;
  uint queueCapacity;
  uint threadsPerGroup;
  uint levelIndex;
  uint maxLevelStats;
  uint maxLeafDrawCount;
};

float particleLodProjectedRadiusPx(uint nodeIndex,
                                   constant float4* nodeCenterRadius,
                                   constant ParticleLodComputeUniforms& uniforms)
{
  float4 centerRadius = nodeCenterRadius[nodeIndex];
  float cameraDistance =
    max(length(centerRadius.xyz - uniforms.cameraPosFocalPx.xyz), 1.0e-6);
  return centerRadius.w * uniforms.cameraPosFocalPx.w / cameraDistance;
}

void particleLodRecordLevelStats(device atomic_uint* levelStats,
                                 constant ParticleLodComputeUniforms& uniforms,
                                 float projectedRadiusPx,
                                 uint eventIndex)
{
  if (uniforms.levelIndex >= uniforms.maxLevelStats) {
    return;
  }
  constexpr uint kFields = 8u;
  constexpr uint kPxScale = 100u;
  constexpr float kMaxRecordedPx = 10000.0f;
  uint base = uniforms.levelIndex * kFields;

  if (eventIndex == 0u) {
    uint px = uint(clamp(projectedRadiusPx, 0.0f, kMaxRecordedPx) *
                  float(kPxScale) + 0.5f);
    atomic_fetch_add_explicit(&levelStats[base + 0u], 1u, memory_order_relaxed);
    atomic_fetch_min_explicit(&levelStats[base + 4u], px, memory_order_relaxed);
    atomic_fetch_max_explicit(&levelStats[base + 5u], px, memory_order_relaxed);
    atomic_fetch_add_explicit(&levelStats[base + 6u], px, memory_order_relaxed);
  } else if (eventIndex <= 3u) {
    atomic_fetch_add_explicit(&levelStats[base + eventIndex],
                              1u,
                              memory_order_relaxed);
  }
}

bool particleLodNodeSmall(uint nodeIndex,
                          constant float4* nodeCenterRadius,
                          constant ParticleLodComputeUniforms& uniforms)
{
  float4 centerRadius = nodeCenterRadius[nodeIndex];
  float focusDistance =
    length(centerRadius.xyz - uniforms.focusPosProtectRadius.xyz);
  float projectedRadiusPx =
    particleLodProjectedRadiusPx(nodeIndex, nodeCenterRadius, uniforms);
  bool smallFromCamera = projectedRadiusPx < uniforms.lodParams.x;
  if (uniforms.lodParams.z > 0.0f) {
    smallFromCamera = projectedRadiusPx < uniforms.lodParams.x *
                                           uniforms.lodParams.z;
  }
  bool outsideProtectedFocus =
    focusDistance > uniforms.focusPosProtectRadius.w;
  return smallFromCamera && outsideProtectedFocus;
}

bool particleLodNodeDefinitelyLarge(uint nodeIndex,
                                    constant float4* nodeCenterRadius,
                                    constant ParticleLodComputeUniforms& uniforms)
{
  float4 centerRadius = nodeCenterRadius[nodeIndex];
  float cameraDistance =
    max(length(centerRadius.xyz - uniforms.cameraPosFocalPx.xyz), 1.0e-6);
  float projectedRadiusPx =
    centerRadius.w * uniforms.cameraPosFocalPx.w / cameraDistance;
  const float expandThreshold =
    uniforms.lodParams.x * max(uniforms.lodParams.w, 1.0f);
  return projectedRadiusPx > expandThreshold;
}

bool particleLodNodeOutsideFrustum(uint nodeIndex,
                                   constant float4* nodeCenterRadius,
                                   constant ParticleLodComputeUniforms& uniforms)
{
  float4 centerRadius = nodeCenterRadius[nodeIndex];
  const float conservativeRadius =
    centerRadius.w * max(uniforms.lodParams.y, 1.0f);
  for (uint i = 0; i < 4u; ++i) {
    float4 plane = uniforms.frustumPlanes[i];
    if (dot(plane.xyz, centerRadius.xyz) + plane.w < -conservativeRadius) {
      return true;
    }
  }
  return false;
}

RenderParticle particleLodRepresentative(uint nodeIndex,
                                         constant float4* representativePosHsml,
                                         constant float4* representativeValue,
                                         constant uint4* representativeMeta)
{
  float4 posHsml = representativePosHsml[nodeIndex];
  uint4 meta = representativeMeta[nodeIndex];
  RenderParticle p;
  p.pos = packed_float3(posHsml.x, posHsml.y, posHsml.z);
  p.type = uchar(meta.x);
  p.flagStress = uchar(meta.y);
  p.pad = ushort(0);
  p.hsml = posHsml.w;
  p.valShow = representativeValue[nodeIndex].x;
  return p;
}

void particleLodAppend(device RenderParticle* outParticles,
                       device RenderParticle* outStressParticles,
                       device atomic_uint* counters,
                       constant ParticleLodComputeUniforms& uniforms,
                       RenderParticle particle)
{
  uint index = atomic_fetch_add_explicit(&counters[0], 1u, memory_order_relaxed);
  if (index < uniforms.maxOutput) {
    outParticles[index] = particle;
  } else {
    atomic_fetch_add_explicit(&counters[2], 1u, memory_order_relaxed);
  }

  if (particle.flagStress != 0) {
    uint stressIndex =
      atomic_fetch_add_explicit(&counters[1], 1u, memory_order_relaxed);
    if (stressIndex < uniforms.maxLeafOutput) {
      outStressParticles[stressIndex] = particle;
    } else {
      atomic_fetch_add_explicit(&counters[2], 1u, memory_order_relaxed);
    }
  }
}

kernel void particleLodResetQueueKernel(device atomic_uint* count [[buffer(0)]],
                                        device uint* dispatchArgs [[buffer(1)]],
                                        uint tid [[thread_position_in_grid]])
{
  if (tid != 0) {
    return;
  }
  atomic_store_explicit(&count[0], 0u, memory_order_relaxed);
  dispatchArgs[0] = 1u;
  dispatchArgs[1] = 1u;
  dispatchArgs[2] = 1u;
}

kernel void particleLodWriteDispatchArgsKernel(
  device atomic_uint* count [[buffer(0)]],
  device uint* dispatchArgs [[buffer(1)]],
  constant ParticleLodComputeUniforms& uniforms [[buffer(2)]],
  device atomic_uint* countToReset [[buffer(3)]],
  uint tid [[thread_position_in_grid]])
{
  if (tid != 0) {
    return;
  }
  uint n = atomic_load_explicit(&count[0], memory_order_relaxed);
  uint tpg = max(uniforms.threadsPerGroup, 1u);
  dispatchArgs[0] = (n + tpg - 1u) / tpg;
  dispatchArgs[1] = 1u;
  dispatchArgs[2] = 1u;
  atomic_store_explicit(&countToReset[0], 0u, memory_order_relaxed);
}

kernel void particleLodFrontierKernel(
  constant RenderParticle* particles [[buffer(0)]],
  constant float4* nodeCenterRadius [[buffer(1)]],
  constant float4* representativePosHsml [[buffer(2)]],
  constant float4* representativeValue [[buffer(3)]],
  constant uint4* nodeMeta [[buffer(4)]],
  constant int4* childA [[buffer(5)]],
  constant int4* childB [[buffer(6)]],
  constant uint4* representativeMeta [[buffer(7)]],
  constant uint* indices [[buffer(8)]],
  device RenderParticle* proxyParticles [[buffer(9)]],
  device atomic_uint* counters [[buffer(10)]],
  constant ParticleLodComputeUniforms& uniforms [[buffer(11)]],
  constant uint* activeQueue [[buffer(12)]],
  device uint* nextQueue [[buffer(13)]],
  constant uint* activeCount [[buffer(14)]],
  device atomic_uint* nextCount [[buffer(15)]],
  device uint4* leafRanges [[buffer(16)]],
  device atomic_uint* levelStats [[buffer(17)]],
  device uint* nodeFlags [[buffer(18)]],
  uint activeIndex [[thread_position_in_grid]])
{
  uint nActive = activeCount[0];
  if (activeIndex >= nActive) {
    return;
  }
  atomic_fetch_add_explicit(&counters[4], 1u, memory_order_relaxed);
  uint nodeIndex = activeQueue[activeIndex];
  if (nodeIndex >= uniforms.nodeCount) {
    return;
  }
  if (uniforms.lodParams.y > 0.5f &&
      particleLodNodeOutsideFrustum(nodeIndex, nodeCenterRadius, uniforms)) {
    atomic_fetch_add_explicit(&counters[5], 1u, memory_order_relaxed);
    return;
  }

  uint4 meta = nodeMeta[nodeIndex];
  bool isLeaf = meta.w == 0u;
  float projectedRadiusPx =
    particleLodProjectedRadiusPx(nodeIndex, nodeCenterRadius, uniforms);
  particleLodRecordLevelStats(levelStats, uniforms, projectedRadiusPx, 0u);
  bool isSmall = particleLodNodeSmall(nodeIndex, nodeCenterRadius, uniforms);
  bool isDefinitelyLarge =
    particleLodNodeDefinitelyLarge(nodeIndex, nodeCenterRadius, uniforms);

  if (isSmall || (!isLeaf && !isDefinitelyLarge)) {
    uint index =
      atomic_fetch_add_explicit(&counters[0], 1u, memory_order_relaxed);
    if (index < uniforms.maxOutput) {
      proxyParticles[index] = particleLodRepresentative(nodeIndex,
                                                        representativePosHsml,
                                                        representativeValue,
                                                        representativeMeta);
    } else {
      atomic_fetch_add_explicit(&counters[2], 1u, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&counters[6], 1u, memory_order_relaxed);
    nodeFlags[nodeIndex] = 1u;
    particleLodRecordLevelStats(levelStats, uniforms, projectedRadiusPx, 1u);
    return;
  }

  if (isLeaf) {
    uint rangeIndex =
      atomic_fetch_add_explicit(&counters[1], 1u, memory_order_relaxed);
    if (rangeIndex < uniforms.queueCapacity) {
      leafRanges[rangeIndex] = uint4(meta.x, meta.y, 0u, 0u);
      atomic_fetch_add_explicit(&counters[3], meta.y, memory_order_relaxed);
      nodeFlags[nodeIndex] = 2u;
    } else {
      atomic_fetch_add_explicit(&counters[2], 1u, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&counters[7], 1u, memory_order_relaxed);
    particleLodRecordLevelStats(levelStats, uniforms, projectedRadiusPx, 2u);
    return;
  }

  atomic_fetch_add_explicit(&counters[8], 1u, memory_order_relaxed);
  particleLodRecordLevelStats(levelStats, uniforms, projectedRadiusPx, 3u);
  int4 ca = childA[nodeIndex];
  int4 cb = childB[nodeIndex];
  int childValues[8] = {ca.x, ca.y, ca.z, ca.w, cb.x, cb.y, cb.z, cb.w};
  for (int i = 0; i < 8; ++i) {
    if (childValues[i] < 0) {
      continue;
    }
    uint write =
      atomic_fetch_add_explicit(&nextCount[0], 1u, memory_order_relaxed);
    if (write < uniforms.queueCapacity) {
      nextQueue[write] = uint(childValues[i]);
      atomic_fetch_add_explicit(&counters[9], 1u, memory_order_relaxed);
    } else {
      atomic_fetch_add_explicit(&counters[2], 1u, memory_order_relaxed);
    }
  }
}

bool particleLodAncestorSmall(uint nodeIndex,
                              constant float4* nodeCenterRadius,
                              constant uint4* representativeMeta,
                              constant ParticleLodComputeUniforms& uniforms)
{
  uint parent = representativeMeta[nodeIndex].z;
  for (uint guard = 0; guard < 64u; ++guard) {
    if (parent == 0xffffffffu || parent >= uniforms.nodeCount) {
      return false;
    }
    if (particleLodNodeSmall(parent, nodeCenterRadius, uniforms)) {
      return true;
    }
    parent = representativeMeta[parent].z;
  }
  return false;
}

kernel void particleLodSinglePassKernel(
  constant RenderParticle* particles [[buffer(0)]],
  constant float4* nodeCenterRadius [[buffer(1)]],
  constant float4* representativePosHsml [[buffer(2)]],
  constant float4* representativeValue [[buffer(3)]],
  constant uint4* nodeMeta [[buffer(4)]],
  constant uint4* representativeMeta [[buffer(5)]],
  constant uint* indices [[buffer(6)]],
  device RenderParticle* outParticles [[buffer(7)]],
  device RenderParticle* outStressParticles [[buffer(8)]],
  device atomic_uint* counters [[buffer(9)]],
  constant ParticleLodComputeUniforms& uniforms [[buffer(10)]],
  uint nodeIndex [[thread_position_in_grid]])
{
  if (nodeIndex >= uniforms.nodeCount) {
    return;
  }
  if (uniforms.lodParams.y > 0.5f &&
      particleLodNodeOutsideFrustum(nodeIndex, nodeCenterRadius, uniforms)) {
    return;
  }
  if (particleLodAncestorSmall(nodeIndex,
                               nodeCenterRadius,
                               representativeMeta,
                               uniforms)) {
    return;
  }

  uint4 meta = nodeMeta[nodeIndex];
  const bool isLeaf = meta.w == 0u;
  const bool isSmall = particleLodNodeSmall(nodeIndex,
                                           nodeCenterRadius,
                                           uniforms);
  if (isSmall) {
    particleLodAppend(outParticles,
                      outStressParticles,
                      counters,
                      uniforms,
                      particleLodRepresentative(nodeIndex,
                                                representativePosHsml,
                                                representativeValue,
                                                representativeMeta));
    return;
  }

  if (!isLeaf) {
    return;
  }
  for (uint i = 0; i < meta.y; ++i) {
    RenderParticle p = particles[indices[meta.x + i]];
    particleLodAppend(outParticles,
                      outStressParticles,
                      counters,
                      uniforms,
                      p);
  }
}

kernel void particleLodFinalizeKernel(
  device atomic_uint* counters [[buffer(0)]],
  device uint4* drawArgs [[buffer(1)]],
  constant ParticleLodComputeUniforms& uniforms [[buffer(2)]],
  uint tid [[thread_position_in_grid]])
{
  if (tid != 0) {
    return;
  }
  uint particleCount =
    min(atomic_load_explicit(&counters[0], memory_order_relaxed),
        uniforms.maxOutput);
  uint leafRangeCount =
    atomic_load_explicit(&counters[1], memory_order_relaxed);
  drawArgs[0] = uint4(particleCount, 1u, 0u, 0u);
  drawArgs[1] = uint4(leafRangeCount, 1u, 0u, 0u);
}

struct VolumeUniforms {
  float4x4 invProjection;
  float4x4 invView;
  float4 cameraForwardFocal;
  float4 rayParams;
  float4 baseColorAndMode;
  float4 tfRangeScale;
  int4 tfControl;
  int4 colorControl;
  float4 opticalParams;
  int4 tfType[4];
  int4 tfLogDomain[4];
  float4 tfCenter[4];
  float4 tfWidth[4];
  float4 tfAmp[4];
};

struct VolumeVertexOut {
  float4 position [[position]];
  float2 ndc;
};

vertex VolumeVertexOut volumeVertex(uint vertexId [[vertex_id]])
{
  float2 pos[3] = {
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0)
  };
  VolumeVertexOut out;
  out.ndc = pos[vertexId];
  out.position = float4(out.ndc, 0.0, 1.0);
  return out;
}

float volumeHeat(float t)
{
  t = clamp(t, 0.0, 1.0);
  float r = smoothstep(0.5, 1.0, t);
  float g = t < 0.5 ? smoothstep(0.0, 0.5, t)
                    : smoothstep(1.0, 0.5, t);
  float b = smoothstep(1.0, 0.5, t);
  return float3(r, g, b).x;
}

float3 volumeHeat3(float t)
{
  t = clamp(t, 0.0, 1.0);
  float r = smoothstep(0.5, 1.0, t);
  float g = t < 0.5 ? smoothstep(0.0, 0.5, t)
                    : smoothstep(1.0, 0.5, t);
  float b = smoothstep(1.0, 0.5, t);
  return float3(r, g, b);
}

int packedInt(constant int4* groups, int i)
{
  return groups[i / 4][i & 3];
}

float packedFloat(constant float4* groups, int i)
{
  return groups[i / 4][i & 3];
}

float gaussianComponent(float value, int i, constant VolumeUniforms& params)
{
  float width = max(packedFloat(params.tfWidth, i), 1.0e-12);
  float center = packedFloat(params.tfCenter, i);
  float x = 0.0;
  if (packedInt(params.tfLogDomain, i) != 0) {
    if (value <= 0.0 || center <= 0.0) {
      return 0.0;
    }
    x = (log10(max(value, 1.0e-30)) -
         log10(max(center, 1.0e-30))) / width;
  } else {
    x = (value - center) / width;
  }
  return packedFloat(params.tfAmp, i) * exp(-0.5 * x * x);
}

float transferNorm(float value, constant VolumeUniforms& params)
{
  float lo = params.tfRangeScale.x;
  float hi = max(params.tfRangeScale.y, lo + 1.0e-6);
  float t = 0.0;
  if (params.tfControl.x != 0) {
    if (value <= 0.0 || lo <= 0.0) {
      return 0.0;
    }
    float llo = log10(max(lo, 1.0e-30));
    float lhi = log10(max(hi, 1.0e-30));
    t = (log10(max(value, 1.0e-30)) - llo) / max(lhi - llo, 1.0e-6);
  } else {
    t = (value - lo) / max(hi - lo, 1.0e-6);
  }
  return clamp(t, 0.0, 1.0);
}

float transferSigma(float value, constant VolumeUniforms& params)
{
  float sigma = 0.0;
  int n = min(max(params.tfControl.y, 0), 16);
  for (int i = 0; i < n; ++i) {
    int type = packedInt(params.tfType, i);
    float center = packedFloat(params.tfCenter, i);
    float width = packedFloat(params.tfWidth, i);
    float amp = packedFloat(params.tfAmp, i);
    if (type == 0) {
      sigma += gaussianComponent(value, i, params);
    } else if (type == 1) {
      sigma += (abs(value - center) <= max(width, 0.0)) ? amp : 0.0;
    } else {
      float dx = abs(value - center);
      float safeWidth = max(width, 1.0e-12);
      sigma += (dx < safeWidth) ? amp * (1.0 - dx / safeWidth) : 0.0;
    }
  }
  return max(params.tfRangeScale.z, 0.0) * max(sigma, 0.0);
}

float3 volumeColor(float value,
                   constant VolumeUniforms& params,
                   texture2d<float> colormapTexture,
                   sampler colormapSampler)
{
  if (params.baseColorAndMode.w > 1.5) {
    float rows = max(float(params.colorControl.y), 1.0);
    float y = (float(max(params.colorControl.x, 0)) + 0.5) / rows;
    return colormapTexture.sample(colormapSampler,
                                  float2(transferNorm(value, params), y)).rgb;
  }
  if (params.baseColorAndMode.w > 0.5) {
    return volumeHeat3(transferNorm(value, params));
  }
  return params.baseColorAndMode.rgb;
}

float screenRadiusPx(float rEff, float zView, float focalPx)
{
  return (zView > 0.0) ? (focalPx * rEff / zView) : 1.0e9;
}

bool rayBox(float3 ro,
            float3 invd,
            float3 mn,
            float3 mx,
            thread float& t0,
            thread float& t1)
{
  float3 t1v = (mn - ro) * invd;
  float3 t2v = (mx - ro) * invd;
  float3 tminv = min(t1v, t2v);
  float3 tmaxv = max(t1v, t2v);
  float lo = max(max(tminv.x, tminv.y), tminv.z);
  float hi = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
  t0 = max(t0, lo);
  t1 = min(t1, hi);
  return t1 >= max(t0, 0.0);
}

float trilerp8(float4 lo, float4 hi, float3 uvw)
{
  float c000 = lo.x;
  float c100 = lo.y;
  float c110 = lo.z;
  float c010 = lo.w;
  float c001 = hi.x;
  float c101 = hi.y;
  float c111 = hi.z;
  float c011 = hi.w;

  float ux = clamp(uvw.x, 0.0, 1.0);
  float uy = clamp(uvw.y, 0.0, 1.0);
  float uz = clamp(uvw.z, 0.0, 1.0);

  float c00 = mix(c000, c100, ux);
  float c10 = mix(c010, c110, ux);
  float c01 = mix(c001, c101, ux);
  float c11 = mix(c011, c111, ux);
  float c0 = mix(c00, c10, uy);
  float c1 = mix(c01, c11, uy);
  return mix(c0, c1, uz);
}

fragment float4 volumeFragment(VolumeVertexOut in [[stage_in]],
                               constant VolumeUniforms& params [[buffer(0)]],
                               constant float4* nodeMinBuffer [[buffer(1)]],
                               constant float4* nodeMaxBuffer [[buffer(2)]],
                               constant int4* childABuffer [[buffer(3)]],
                               constant int4* childBBuffer [[buffer(4)]],
                               constant float4* cornerLoBuffer [[buffer(5)]],
                               constant float4* cornerHiBuffer [[buffer(6)]],
                               texture2d<float> colormapTexture [[texture(0)]],
                               sampler colormapSampler [[sampler(0)]])
{
  int root = params.tfControl.z;
  if (root < 0) {
    return float4(0.0);
  }

  float2 ndc = in.ndc;
  float4 pN = params.invProjection * float4(ndc, -1.0, 1.0);
  pN /= pN.w;
  float3 ro = (params.invView * float4(0.0, 0.0, 0.0, 1.0)).xyz;
  float3 rd = normalize((params.invView * float4(pN.xyz, 0.0)).xyz);
  float3 invd = 1.0 / max(abs(rd), float3(1.0e-30)) * sign(rd);

  float3 rootMin = nodeMinBuffer[root].xyz;
  float3 rootMax = nodeMaxBuffer[root].xyz;
  float t0 = 0.0;
  float t1 = 1.0e30;
  if (!rayBox(ro, invd, rootMin, rootMax, t0, t1)) {
    return float4(0.0);
  }

  const int STACK_MAX = 64;
  int stack[STACK_MAX];
  float t0s[STACK_MAX];
  float t1s[STACK_MAX];
  int sp = 0;
  stack[sp] = root;
  t0s[sp] = t0;
  t1s[sp] = t1;
  sp++;

  float alpha = 0.0;
  float3 color = float3(0.0);

  int visits = 0;
  int leafStops = 0;
  int lodStops = 0;
  int childHits = 0;
  int emptySkips = 0;
  const int MAX_VISITS = 1000;

  while (sp > 0 && alpha < 0.995 && visits < MAX_VISITS) {
    int id = stack[--sp];
    t0 = t0s[sp];
    t1 = t1s[sp];
    visits++;

    float4 nodeMin = nodeMinBuffer[id];
    float4 nodeMax = nodeMaxBuffer[id];
    float3 bmin = nodeMin.xyz;
    float3 bmax = nodeMax.xyz;

    float radius = 0.5 * length(bmax - bmin);
    float3 center = 0.5 * (bmin + bmax);
    float zView = dot(center - ro, params.cameraForwardFocal.xyz);
    float rPx = screenRadiusPx(radius, zView, params.cameraForwardFocal.w);

    int4 cA = childABuffer[id];
    int4 cB = childBBuffer[id];
    bool isLeaf = cA.x < 0 && cA.y < 0 && cA.z < 0 && cA.w < 0 &&
                  cB.x < 0 && cB.y < 0 && cB.z < 0 && cB.w < 0;

    if (params.tfRangeScale.w <= 0.0 ||
        params.tfRangeScale.w * max(0.0, t1 - t0) < params.rayParams.w) {
      emptySkips++;
      continue;
    }

    bool useLod = params.rayParams.x > 0.0 &&
                  !isLeaf &&
                  rPx < 2.0 * params.rayParams.x;
    if (isLeaf || useLod) {
      if (isLeaf) {
        leafStops++;
      } else {
        lodStops++;
      }

      float interval = max(0.0, t1 - t0);
      float requestedStep = params.rayParams.z;
      int maxSamples = int(clamp(params.opticalParams.w, 1.0, 256.0));
      int sampleCount = (requestedStep > 0.0)
        ? int(clamp(ceil(interval / requestedStep), 1.0, float(maxSamples)))
        : 1;
      float dt = interval / float(sampleCount);
      float3 size = max(bmax - bmin, float3(1.0e-8));
      int opticalModel = int(params.opticalParams.x + 0.5);
      for (int sampleIndex = 0;
           sampleIndex < 256 && sampleIndex < sampleCount;
           ++sampleIndex) {
        float ts = t0 + (float(sampleIndex) + 0.5) * dt;
        float3 pmid = ro + rd * ts;
        float3 uvw = clamp((pmid - bmin) / size, 0.0, 1.0);
        float value = trilerp8(cornerLoBuffer[id],
                               cornerHiBuffer[id],
                               uvw);
        float sigma = transferSigma(value, params);
        float3 tfc = volumeColor(value,
                                 params,
                                 colormapTexture,
                                 colormapSampler);
        if (opticalModel == 0) {
          float a = 1.0 - exp(-sigma * dt);
          color += (1.0 - alpha) * a * tfc;
          alpha = 1.0 - (1.0 - alpha) * (1.0 - a);
        } else {
          float transmittance = 1.0 - alpha;
          float emission = sigma * max(params.opticalParams.y, 0.0);
          float absorption =
            (opticalModel == 2) ? sigma * max(params.opticalParams.z, 0.0)
                                : 0.0;
          color += transmittance * tfc * emission * dt;
          if (absorption > 0.0) {
            float a = 1.0 - exp(-absorption * dt);
            alpha = 1.0 - (1.0 - alpha) * (1.0 - a);
          } else {
            alpha = max(alpha,
                        clamp(max(max(color.r, color.g), color.b),
                              0.0,
                              0.995));
          }
        }
        if (alpha >= 0.995) {
          break;
        }
      }
      continue;
    }

    int childIdx[8] = {
      cA.x, cA.y, cA.z, cA.w, cB.x, cB.y, cB.z, cB.w
    };

    int hitId[8];
    float hitT0[8];
    float hitT1[8];
    int hitCount = 0;

    for (int k = 0; k < 8; ++k) {
      int cid = childIdx[k];
      if (cid < 0) {
        continue;
      }
      float3 childMin = nodeMinBuffer[cid].xyz;
      float3 childMax = nodeMaxBuffer[cid].xyz;
      float c0 = t0;
      float c1 = t1;
      if (!rayBox(ro, invd, childMin, childMax, c0, c1)) {
        continue;
      }
      if (params.tfRangeScale.w <= 0.0 ||
          params.tfRangeScale.w * max(0.0, c1 - c0) < params.rayParams.w) {
        emptySkips++;
        continue;
      }
      childHits++;
      hitId[hitCount] = cid;
      hitT0[hitCount] = c0;
      hitT1[hitCount] = c1;
      hitCount++;
    }

    for (int i = 1; i < hitCount; ++i) {
      int idv = hitId[i];
      float t0v = hitT0[i];
      float t1v = hitT1[i];
      int j = i - 1;
      while (j >= 0 && hitT0[j] > t0v) {
        hitId[j + 1] = hitId[j];
        hitT0[j + 1] = hitT0[j];
        hitT1[j + 1] = hitT1[j];
        j--;
      }
      hitId[j + 1] = idv;
      hitT0[j + 1] = t0v;
      hitT1[j + 1] = t1v;
    }

    for (int i = hitCount - 1; i >= 0; --i) {
      if (sp < STACK_MAX) {
        stack[sp] = hitId[i];
        t0s[sp] = hitT0[i];
        t1s[sp] = hitT1[i];
        sp++;
      }
    }
  }

  if (params.tfControl.w == 10) {
    return float4(volumeHeat3(float(visits) / 100.0), 1.0);
  }
  if (params.tfControl.w == 11) {
    return float4(volumeHeat3(clamp(float(leafStops) / 64.0, 0.0, 1.0)), 1.0);
  }
  if (params.tfControl.w == 12) {
    return float4(volumeHeat3(clamp(float(lodStops) / 64.0, 0.0, 1.0)), 1.0);
  }
  if (params.tfControl.w == 13) {
    return float4(volumeHeat3(clamp(float(emptySkips) / 64.0, 0.0, 1.0)), 1.0);
  }
  if (params.tfControl.w == 14) {
    return float4(float3(alpha), 1.0);
  }
  if (params.tfControl.w == 20) {
    return float4(float(visits), float(childHits), float(leafStops), 1.0);
  }

  return float4(color, alpha);
}
)";

struct alignas(16) ParticleUniformsCpu {
  float mvp[16] = {};
  float typeParamsA[6][4] = {};
  float typeParamsB[6][4] = {};
  float typeColors[6][4] = {};
  float misc[4] = {1.0f, 1.0f, 1.0f, 0.0f};
};

struct alignas(16) ParticleLodComputeUniformsCpu {
  float cameraPosFocalPx[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float focusPosProtectRadius[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float lodParams[4] = {0.75f, 0.0f, 0.0f, 0.0f};
  float frustumPlanes[6][4] = {};
  std::uint32_t nodeCount = 0;
  std::uint32_t maxOutput = 0;
  std::uint32_t maxLeafOutput = 0;
  std::uint32_t queueCapacity = 0;
  std::uint32_t threadsPerGroup = 1;
  std::uint32_t levelIndex = 0;
  std::uint32_t maxLevelStats = 0;
  std::uint32_t maxLeafDrawCount = 1;
};

struct alignas(16) ParticleRangeUniformsCpu {
  std::uint32_t maxLeafCount = 1;
  std::uint32_t pad[3] = {};
};

struct alignas(16) LineUniformsCpu {
  float mvp[16] = {};
  float opacityScale = 1.0f;
  float pad[3] = {};
};

struct alignas(16) SolidUniformsCpu {
  float viewProjection[16] = {};
  float opacityScale = 1.0f;
  float pad[3] = {};
};

#ifdef VOLUME_RENDERING
constexpr std::size_t kVolumeTransferGroups = 4;
constexpr std::size_t kVolumeTransferSlots = kVolumeTransferGroups * 4;

struct alignas(16) MetalInt4 {
  int v[4] = {};
};

struct alignas(16) MetalFloat4 {
  float v[4] = {};
};

struct alignas(16) VolumeUniformsCpu {
  float invProjection[16] = {};
  float invView[16] = {};
  float cameraForwardFocal[4] = {0.0f, 0.0f, -1.0f, 1.0f};
  float rayParams[4] = {2.0f, 1.0f, 0.0f, 1.0e-4f};
  float baseColorAndMode[4] = {0.6f, 0.7f, 1.0f, 0.0f};
  float tfRangeScale[4] = {1.0e-6f, 1.0f, 1.0f, 0.0f};
  MetalInt4 tfControl;
  MetalInt4 colorControl;
  float opticalParams[4] = {0.0f, 1.0f, 1.0f, 32.0f};
  MetalInt4 tfType[kVolumeTransferGroups];
  MetalInt4 tfLogDomain[kVolumeTransferGroups];
  MetalFloat4 tfCenter[kVolumeTransferGroups];
  MetalFloat4 tfWidth[kVolumeTransferGroups];
  MetalFloat4 tfAmp[kVolumeTransferGroups];
};

static_assert(sizeof(MetalInt4) == 16);
static_assert(sizeof(MetalFloat4) == 16);
#endif

struct MetalLineVertex {
  float pos[4] = {};
  float color[4] = {};
};

struct MetalSolidVertex {
  float pos[4] = {};
};

struct MetalSolidInstance {
  float model[16] = {};
  float colorOpacity[4] = {};
};

void CopyMat4(const glm::mat4& matrix, float out[16])
{
  std::memcpy(out, &matrix[0][0], sizeof(float) * 16);
}

float Lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

std::array<unsigned char, 4> SampleColormap(const ColormapDef& def, float t)
{
  if (!def.data || def.count <= 0) {
    return {255, 255, 255, 255};
  }
  if (def.count == 1) {
    return {
      static_cast<unsigned char>(std::clamp(def.data[0], 0.0f, 1.0f) * 255.0f),
      static_cast<unsigned char>(std::clamp(def.data[1], 0.0f, 1.0f) * 255.0f),
      static_cast<unsigned char>(std::clamp(def.data[2], 0.0f, 1.0f) * 255.0f),
      255
    };
  }

  const float x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(def.count - 1);
  const int i0 = std::clamp(static_cast<int>(x), 0, def.count - 1);
  const int i1 = std::min(i0 + 1, def.count - 1);
  const float f = x - static_cast<float>(i0);
  std::array<unsigned char, 4> rgba{};
  for (int c = 0; c < 3; ++c) {
    const float v0 = def.data[i0 * 3 + c];
    const float v1 = def.data[i1 * 3 + c];
    rgba[c] = static_cast<unsigned char>(
      std::clamp(Lerp(v0, v1, f), 0.0f, 1.0f) * 255.0f);
  }
  rgba[3] = 255;
  return rgba;
}

ImU32 SampleColormapColorU32(int colormapIndex, float t)
{
  const ColormapDef* defs = AvailableColormaps();
  const int count = AvailableColormapCount();
  if (!defs || count <= 0) {
    return IM_COL32(255, 255, 255, 255);
  }
  const int index = std::clamp(colormapIndex, 0, count - 1);
  const std::array<unsigned char, 4> rgba = SampleColormap(defs[index], t);
  return IM_COL32(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void FillParticleUniforms(const RenderFrameState& frame,
                          ParticleUniformsCpu& uniforms)
{
  glm::mat4 metalClipFromOpenGL(1.0f);
  metalClipFromOpenGL[2][2] = 0.5f;
  metalClipFromOpenGL[3][2] = 0.5f;
  CopyMat4(metalClipFromOpenGL * frame.matrices.projection *
             frame.matrices.view *
             frame.matrices.model,
           uniforms.mvp);

  static constexpr float kTypeColors[6][4] = {
    {1.0f, 0.52f, 0.05f, 1.0f},
    {0.1f, 0.3f, 1.0f, 1.0f},
    {0.2f, 1.0f, 0.65f, 1.0f},
    {1.0f, 0.2f, 0.2f, 1.0f},
    {1.0f, 0.85f, 0.2f, 1.0f},
    {0.85f, 0.45f, 1.0f, 1.0f},
  };

  for (int i = 0; i < kNumParticleTypes; ++i) {
    const ParticleTypeVisualConfig& cfg = frame.particleVisual.types[i];
    uniforms.typeParamsA[i][0] = cfg.pointSize;
    uniforms.typeParamsA[i][1] = cfg.colorMin;
    uniforms.typeParamsA[i][2] = cfg.colorMax;
    uniforms.typeParamsA[i][3] = cfg.useLogScale ? 1.0f : 0.0f;
    const int colorIndex =
      std::clamp(cfg.colormapIndex, 0, std::max(0, gNumColormaps - 1));
    uniforms.typeParamsB[i][0] = cfg.periodicColorBar ? 1.0f : 0.0f;
    uniforms.typeParamsB[i][1] = cfg.hideParticles ? 1.0f : 0.0f;
    uniforms.typeParamsB[i][2] = static_cast<float>(colorIndex);
    uniforms.typeParamsB[i][3] = 0.0f;
    std::memcpy(uniforms.typeColors[i], kTypeColors[i], sizeof(float) * 4);
  }
  uniforms.misc[0] = 1.0f;
  uniforms.misc[1] = 1.0f;
  uniforms.misc[2] = static_cast<float>(std::max(1, gNumColormaps));
  uniforms.misc[3] = 0.0f;
}

void FillLineUniforms(const RenderFrameState& frame,
                      float opacity,
                      LineUniformsCpu& uniforms)
{
  glm::mat4 metalClipFromOpenGL(1.0f);
  metalClipFromOpenGL[2][2] = 0.5f;
  metalClipFromOpenGL[3][2] = 0.5f;
  CopyMat4(metalClipFromOpenGL * frame.matrices.projection *
             frame.matrices.view *
             frame.matrices.model,
           uniforms.mvp);
  uniforms.opacityScale = std::clamp(opacity, 0.0f, 1.0f);
}

void FillSolidUniforms(const RenderFrameState& frame,
                       float opacity,
                       SolidUniformsCpu& uniforms)
{
  glm::mat4 metalClipFromOpenGL(1.0f);
  metalClipFromOpenGL[2][2] = 0.5f;
  metalClipFromOpenGL[3][2] = 0.5f;
  CopyMat4(metalClipFromOpenGL * frame.matrices.projection *
             frame.matrices.view,
           uniforms.viewProjection);
  uniforms.opacityScale = std::clamp(opacity, 0.0f, 1.0f);
}

#ifdef VOLUME_RENDERING
void FillVolumeUniforms(const RenderFrameState& frame,
                        int root,
                        float focalScale,
                        VolumeUniformsCpu& uniforms)
{
  const RenderRuntimeState& render = frame.runtime;
  CopyMat4(frame.matrices.invProj, uniforms.invProjection);
  CopyMat4(frame.matrices.invView, uniforms.invView);
  uniforms.cameraForwardFocal[0] = frame.matrices.camForward.x;
  uniforms.cameraForwardFocal[1] = frame.matrices.camForward.y;
  uniforms.cameraForwardFocal[2] = frame.matrices.camForward.z;
  uniforms.cameraForwardFocal[3] = frame.matrices.focalPx * focalScale;
  uniforms.rayParams[0] = render.volume.pixelThreshold;
  uniforms.rayParams[1] = render.volume.tauMax;
  uniforms.rayParams[2] = render.volume.stepBias;
  uniforms.rayParams[3] = render.volume.skipEpsilon;
  uniforms.baseColorAndMode[0] = render.volume.baseColor.r;
  uniforms.baseColorAndMode[1] = render.volume.baseColor.g;
  uniforms.baseColorAndMode[2] = render.volume.baseColor.b;
  uniforms.baseColorAndMode[3] =
    static_cast<float>(std::clamp(render.volume.colorMode, 0, 2));
  uniforms.tfRangeScale[0] = render.volume.tfValueMin;
  uniforms.tfRangeScale[1] = render.volume.tfValueMax;
  uniforms.tfRangeScale[2] = render.volume.tfSigmaScale;
  uniforms.tfRangeScale[3] = render.volume.tfMaxSigma;
  uniforms.tfControl.v[0] = render.volume.tfLogScale ? 1 : 0;
  uniforms.tfControl.v[1] =
    std::min(static_cast<int>(render.volume.tfComponents.size()),
             static_cast<int>(kVolumeTransferSlots));
  uniforms.tfControl.v[2] = root;
  uniforms.tfControl.v[3] = render.volume.debugMode;
  uniforms.colorControl.v[0] =
    std::clamp(render.volume.colormapIndex,
               0,
               std::max(0, AvailableColormapCount() - 1));
  uniforms.colorControl.v[1] = std::max(1, AvailableColormapCount());
  uniforms.opticalParams[0] =
    static_cast<float>(std::clamp(render.volume.opticalModel, 0, 2));
  uniforms.opticalParams[1] = std::max(render.volume.emissionScale, 0.0f);
  uniforms.opticalParams[2] = std::max(render.volume.absorptionScale, 0.0f);
  uniforms.opticalParams[3] =
    static_cast<float>(std::clamp(render.volume.maxSamplesPerCell, 1, 256));

  for (std::size_t i = 0; i < kVolumeTransferSlots; ++i) {
    const std::size_t group = i / 4u;
    const std::size_t slot = i & 3u;
    if (i < render.volume.tfComponents.size()) {
      const auto& comp = render.volume.tfComponents[i];
      uniforms.tfType[group].v[slot] = comp.type;
      uniforms.tfLogDomain[group].v[slot] = comp.logDomain ? 1 : 0;
      uniforms.tfCenter[group].v[slot] = comp.center;
      uniforms.tfWidth[group].v[slot] = comp.width;
      uniforms.tfAmp[group].v[slot] = comp.amplitude;
    }
  }
}
#endif

std::vector<float> BuildColormapTexturePixels()
{
  constexpr int kSamples = 256;
  const int rows = std::max(1, gNumColormaps);
  std::vector<float> pixels(static_cast<std::size_t>(kSamples) *
                            static_cast<std::size_t>(rows) * 4u,
                            1.0f);
  for (int row = 0; row < rows; ++row) {
    const ColormapDef& cmap = gColormapDefs[row];
    for (int x = 0; x < kSamples; ++x) {
      const float t =
        static_cast<float>(x) / static_cast<float>(kSamples - 1);
      const float scaled = t * static_cast<float>(std::max(1, cmap.count - 1));
      const int i0 = std::clamp(static_cast<int>(scaled), 0, cmap.count - 1);
      const int i1 = std::clamp(i0 + 1, 0, cmap.count - 1);
      const float f = scaled - static_cast<float>(i0);
      const std::size_t out =
        (static_cast<std::size_t>(row) * kSamples +
         static_cast<std::size_t>(x)) * 4u;
      for (int c = 0; c < 3; ++c) {
        const float a = cmap.data[i0 * 3 + c];
        const float b = cmap.data[i1 * 3 + c];
        pixels[out + c] = a * (1.0f - f) + b * f;
      }
      pixels[out + 3] = 1.0f;
    }
  }
  return pixels;
}

struct ColorbarLayoutPixels {
  float left = 0.0f;
  float right = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
};

ColorbarLayoutPixels ComputeColorbarLayoutPixels(
  const RenderFrameState& frame)
{
  const auto& settings = frame.runtime.colorbar.layout;
  const float width = static_cast<float>(frame.viewport.width);
  const float height = static_cast<float>(frame.viewport.height);

  ColorbarLayoutPixels layout;
  layout.left = width - settings.width - settings.margin;
  layout.right = width - settings.margin;
  layout.bottom = height - settings.margin;
  layout.top = height - settings.height - settings.margin;
  layout.offsetX = static_cast<float>(frame.viewport.x);
  layout.offsetY = static_cast<float>(frame.viewport.y);
  return layout;
}

ImVec2 PhysicalToImGui(float x, float y)
{
  const ImGuiIO& io = ImGui::GetIO();
  const float scaleX = io.DisplayFramebufferScale.x > 0.0f
                         ? io.DisplayFramebufferScale.x
                         : 1.0f;
  const float scaleY = io.DisplayFramebufferScale.y > 0.0f
                         ? io.DisplayFramebufferScale.y
                         : 1.0f;
  return ImVec2(x / scaleX, y / scaleY);
}

ImVec2 PhysicalToImGui(float x, float y, const ColorbarLayoutPixels& layout)
{
  return PhysicalToImGui(x + layout.offsetX, y + layout.offsetY);
}

glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
{
  const float len2 = glm::dot(v, v);
  if (len2 <= 1.0e-20f) {
    return fallback;
  }
  return v / std::sqrt(len2);
}

bool ProjectWorldToImGui(const RenderFrameState& frame,
                         const glm::vec3& pos,
                         ImVec2& out)
{
  if (!frame.valid || frame.viewport.width <= 0 || frame.viewport.height <= 0) {
    return false;
  }

  const glm::mat4 mvp =
    frame.matrices.projection * frame.matrices.view * frame.matrices.model;
  const glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
  if (clip.w <= 1.0e-8f) {
    return false;
  }

  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (ndc.z < -1.0f || ndc.z > 1.0f) {
    return false;
  }

  const float x =
    static_cast<float>(frame.viewport.x) +
    (ndc.x * 0.5f + 0.5f) * static_cast<float>(frame.viewport.width);
  const float y =
    static_cast<float>(frame.viewport.y) +
    (1.0f - (ndc.y * 0.5f + 0.5f)) *
      static_cast<float>(frame.viewport.height);
  out = PhysicalToImGui(x, y);
  return true;
}

void FillFrustumPlanes(const glm::mat4& clipFromWorld,
                       float outPlanes[6][4])
{
  const glm::vec4 row0(clipFromWorld[0][0],
                       clipFromWorld[1][0],
                       clipFromWorld[2][0],
                       clipFromWorld[3][0]);
  const glm::vec4 row1(clipFromWorld[0][1],
                       clipFromWorld[1][1],
                       clipFromWorld[2][1],
                       clipFromWorld[3][1]);
  const glm::vec4 row2(clipFromWorld[0][2],
                       clipFromWorld[1][2],
                       clipFromWorld[2][2],
                       clipFromWorld[3][2]);
  const glm::vec4 row3(clipFromWorld[0][3],
                       clipFromWorld[1][3],
                       clipFromWorld[2][3],
                       clipFromWorld[3][3]);
  const glm::vec4 planes[6] = {
    row3 + row0,
    row3 - row0,
    row3 + row1,
    row3 - row1,
    row3 + row2,
    row3 - row2
  };

  for (int i = 0; i < 6; ++i) {
    const glm::vec3 normal(planes[i]);
    const float invLen =
      1.0f / std::max(std::sqrt(glm::dot(normal, normal)), 1.0e-20f);
    outPlanes[i][0] = planes[i].x * invLen;
    outPlanes[i][1] = planes[i].y * invLen;
    outPlanes[i][2] = planes[i].z * invLen;
    outPlanes[i][3] = planes[i].w * invLen;
  }
}

bool EqualMatrix(const glm::mat4& a, const glm::mat4& b)
{
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (a[c][r] != b[c][r]) {
        return false;
      }
    }
  }
  return true;
}

struct ParticleLodDrawRange {
  std::uint32_t start = 0;
  std::uint32_t count = 0;
};

constexpr std::size_t kMetalParticleLodMaxIcbCommands = 32768;
constexpr std::size_t kMetalParticleLodTargetRangeDrawCommands = 2048;
constexpr std::uint64_t kMetalParticleLodStableMergeGapParticles = 4096;
constexpr double kMetalParticleLodNormalFallbackCoverage = 0.9;

void SortAndMergeParticleLodRanges(std::vector<ParticleLodDrawRange>& ranges)
{
  std::sort(ranges.begin(),
            ranges.end(),
            [](const ParticleLodDrawRange& a,
               const ParticleLodDrawRange& b) {
              return a.start < b.start;
            });

  std::size_t write = 0;
  for (const ParticleLodDrawRange& range : ranges) {
    if (range.count == 0) {
      continue;
    }
    if (write > 0) {
      ParticleLodDrawRange& last = ranges[write - 1];
      const std::uint64_t lastEnd =
        static_cast<std::uint64_t>(last.start) +
        static_cast<std::uint64_t>(last.count);
      if (lastEnd >= range.start) {
        const std::uint64_t rangeEnd =
          static_cast<std::uint64_t>(range.start) +
          static_cast<std::uint64_t>(range.count);
        last.count =
          static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
              std::numeric_limits<std::uint32_t>::max(),
              std::max(lastEnd, rangeEnd) -
                static_cast<std::uint64_t>(last.start)));
        continue;
      }
    }
    ranges[write++] = range;
  }
  ranges.resize(write);
}

void MergeSortedParticleLodRanges(std::vector<ParticleLodDrawRange>& ranges)
{
  std::size_t write = 0;
  for (const ParticleLodDrawRange& range : ranges) {
    if (range.count == 0) {
      continue;
    }
    if (write > 0) {
      ParticleLodDrawRange& last = ranges[write - 1];
      const std::uint64_t lastEnd =
        static_cast<std::uint64_t>(last.start) +
        static_cast<std::uint64_t>(last.count);
      if (lastEnd >= range.start) {
        const std::uint64_t rangeEnd =
          static_cast<std::uint64_t>(range.start) +
          static_cast<std::uint64_t>(range.count);
        last.count =
          static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
              std::numeric_limits<std::uint32_t>::max(),
              std::max(lastEnd, rangeEnd) -
                static_cast<std::uint64_t>(last.start)));
        continue;
      }
    }
    ranges[write++] = range;
  }
  ranges.resize(write);
}

void MergeParticleLodRangeUnion(
  const std::vector<ParticleLodDrawRange>& a,
  const std::vector<ParticleLodDrawRange>& b,
  std::vector<ParticleLodDrawRange>& out)
{
  out.clear();
  out.reserve(a.size() + b.size());
  std::merge(a.begin(),
             a.end(),
             b.begin(),
             b.end(),
             std::back_inserter(out),
             [](const ParticleLodDrawRange& lhs,
                const ParticleLodDrawRange& rhs) {
               return lhs.start < rhs.start;
             });
  MergeSortedParticleLodRanges(out);
}

std::uint64_t CountParticleLodRangeVertices(
  const std::vector<ParticleLodDrawRange>& ranges)
{
  std::uint64_t total = 0;
  for (const ParticleLodDrawRange& range : ranges) {
    total += range.count;
  }
  return total;
}

void CoalesceParticleLodRangesToBudget(
  std::vector<ParticleLodDrawRange>& ranges,
  std::size_t targetRangeCount,
  std::uint64_t maxDrawnParticleCount)
{
  if (ranges.empty() || targetRangeCount == 0) {
    return;
  }

  std::uint64_t drawnParticleCount = CountParticleLodRangeVertices(ranges);
  std::vector<ParticleLodDrawRange> merged;
  merged.reserve(std::min(ranges.size(), targetRangeCount));
  ParticleLodDrawRange current = ranges.front();
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    const ParticleLodDrawRange& next = ranges[i];
    const std::uint64_t currentEnd =
      static_cast<std::uint64_t>(current.start) + current.count;
    const std::uint64_t gap =
      next.start > currentEnd
        ? static_cast<std::uint64_t>(next.start) - currentEnd
        : 0u;
    const std::uint64_t mergedEnd =
      static_cast<std::uint64_t>(next.start) + next.count;
    const std::uint64_t mergedCount =
      mergedEnd - static_cast<std::uint64_t>(current.start);
    const std::uint64_t extraDraw =
      mergedCount -
      static_cast<std::uint64_t>(current.count) -
      static_cast<std::uint64_t>(next.count);
    const bool canMerge =
      gap <= kMetalParticleLodStableMergeGapParticles &&
      drawnParticleCount + extraDraw <= maxDrawnParticleCount &&
      mergedCount <= std::numeric_limits<std::uint32_t>::max();
    if (canMerge) {
      current.count = static_cast<std::uint32_t>(mergedCount);
      drawnParticleCount += extraDraw;
    } else {
      merged.push_back(current);
      current = next;
    }
  }
  merged.push_back(current);
  ranges.swap(merged);
}

bool EqualParticleTypeVisualConfig(const ParticleTypeVisualConfig& a,
                                   const ParticleTypeVisualConfig& b)
{
  return a.selectedQuantity == b.selectedQuantity &&
         a.pointSize == b.pointSize &&
         a.colorMin == b.colorMin &&
         a.colorMax == b.colorMax &&
         a.useLogScale == b.useLogScale &&
         a.hideParticles == b.hideParticles &&
         a.periodicColorBar == b.periodicColorBar &&
         a.colormapIndex == b.colormapIndex;
}

bool EqualParticleVisualConfig(const ParticleVisualConfig& a,
                               const ParticleVisualConfig& b)
{
  for (int i = 0; i < kNumParticleTypes; ++i) {
    if (!EqualParticleTypeVisualConfig(a.types[i], b.types[i])) {
      return false;
    }
  }
  return true;
}

void DrawProjectedWorldLine(ImDrawList& drawList,
                            const RenderFrameState& frame,
                            const glm::vec3& a,
                            const glm::vec3& b,
                            ImU32 color,
                            float thickness)
{
  ImVec2 p0;
  ImVec2 p1;
  if (ProjectWorldToImGui(frame, a, p0) &&
      ProjectWorldToImGui(frame, b, p1)) {
    drawList.AddLine(p0, p1, color, thickness);
  }
}

void DrawCrossGizmoOverlay(ImDrawList& drawList,
                           const RenderFrameState& frame)
{
  const auto& cross = frame.runtime.crossGizmo;
  if (!cross.show) {
    return;
  }

  const glm::vec3 forward =
    SafeNormalize(frame.camera.cameraTarget - frame.camera.cameraPos,
                  glm::vec3(0.0f, 0.0f, -1.0f));
  const glm::vec3 right =
    SafeNormalize(glm::cross(forward, frame.camera.cameraUp),
                  glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::vec3 up =
    SafeNormalize(glm::cross(right, forward),
                  glm::vec3(0.0f, 1.0f, 0.0f));

  const glm::vec3 center = frame.camera.cameraTarget;
  const float size = std::max(cross.size, 0.0f);
  constexpr float kCrossThickness = 2.0f;
  const ImU32 white = IM_COL32(255, 255, 255, 235);
  DrawProjectedWorldLine(drawList,
                         frame,
                         center - (right + up) * size,
                         center + (right + up) * size,
                         white,
                         kCrossThickness);
  DrawProjectedWorldLine(drawList,
                         frame,
                         center - (right - up) * size,
                         center + (right - up) * size,
                         white,
                         kCrossThickness);
  DrawProjectedWorldLine(drawList,
                         frame,
                         center - up * size,
                         center + up * size,
                         white,
                         kCrossThickness);
}

void DrawCoordAxesOverlay(ImDrawList& drawList,
                          const RenderFrameState& frame)
{
  if (!frame.runtime.coordAxes.show || frame.viewport.width <= 0 ||
      frame.viewport.height <= 0) {
    return;
  }

  const float axisLength =
    0.085f * static_cast<float>(std::min(frame.viewport.width,
                                         frame.viewport.height));
  const float originX =
    static_cast<float>(frame.viewport.x) +
    static_cast<float>(frame.viewport.width) * 0.90f;
  const float originY =
    static_cast<float>(frame.viewport.y) +
    static_cast<float>(frame.viewport.height) * 0.84f;
  const ImVec2 origin = PhysicalToImGui(originX, originY);
  const glm::mat3 viewRot(frame.matrices.view);

  auto drawAxis = [&](const glm::vec3& axis,
                      ImU32 color,
                      const char* label) {
    const glm::vec3 rotated = viewRot * axis;
    const ImVec2 end = PhysicalToImGui(originX + rotated.x * axisLength,
                                       originY - rotated.y * axisLength);
    drawList.AddLine(origin, end, color, 3.0f);
    drawList.AddText(ImVec2(end.x + 4.0f, end.y + 4.0f), color, label);
  };

  drawAxis(glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(255, 80, 80, 240), "X");
  drawAxis(glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(80, 255, 80, 240), "Y");
  drawAxis(glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(240, 240, 255, 240), "Z");
}

void DrawGizmoOverlays(const RenderFrameState& frame)
{
  if (!frame.valid) {
    return;
  }
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  if (!drawList) {
    return;
  }
  DrawCrossGizmoOverlay(*drawList, frame);
  DrawCoordAxesOverlay(*drawList, frame);
}

void DrawColorbarOverlay(const RenderFrameState& frame)
{
  if (!frame.valid || !frame.runtime.colorbar.show) {
    return;
  }
  const int particleType =
    std::clamp(frame.runtime.colorbar.sourceParticleType,
               0,
               kNumParticleTypes - 1);
  const auto& visual = frame.particleVisual.types[particleType];
  const ColorbarLayoutPixels layout = ComputeColorbarLayoutPixels(frame);
  if (layout.right <= layout.left || layout.bottom <= layout.top) {
    return;
  }

  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  if (!drawList) {
    return;
  }

  constexpr int kSegments = 96;
  for (int i = 0; i < kSegments; ++i) {
    const float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
    const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
    const float x0 = layout.left + t0 * (layout.right - layout.left);
    const float x1 = layout.left + t1 * (layout.right - layout.left);
    const ImVec2 p0 = PhysicalToImGui(x0, layout.top, layout);
    const ImVec2 p1 = PhysicalToImGui(x1, layout.bottom, layout);
    const ImU32 c0 = SampleColormapColorU32(visual.colormapIndex, t0);
    const ImU32 c1 = SampleColormapColorU32(visual.colormapIndex, t1);
    drawList->AddRectFilledMultiColor(p0, p1, c0, c1, c1, c0);
  }

  const ImVec2 borderMin = PhysicalToImGui(layout.left, layout.top, layout);
  const ImVec2 borderMax = PhysicalToImGui(layout.right, layout.bottom, layout);
  drawList->AddRect(borderMin, borderMax, IM_COL32(255, 255, 255, 220));

  const int numTicks = std::max(frame.runtime.colorbar.numTicks, 2);
  for (int i = 0; i < numTicks; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(numTicks - 1);
    const float x = layout.left + t * (layout.right - layout.left);
    const float value = visual.colorMin + t * (visual.colorMax - visual.colorMin);
    const ImVec2 tick0 = PhysicalToImGui(x, layout.bottom, layout);
    const ImVec2 tick1 = PhysicalToImGui(x, layout.bottom + 4.0f, layout);
    drawList->AddLine(tick0, tick1, IM_COL32(255, 255, 255, 220));

    char label[32];
    std::snprintf(label, sizeof(label), "%.2f", value);
    ImVec2 textPos = PhysicalToImGui(x, layout.bottom + 7.0f, layout);
    textPos.x = std::floor(textPos.x + 0.5f);
    textPos.y = std::floor(textPos.y + 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label);
  }
}

const std::vector<std::pair<int, int>>& AllCuboidEdges()
{
  static const std::vector<std::pair<int, int>> edges = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  return edges;
}

const std::vector<std::pair<int, int>>& SelectedCuboidAxisEdges(
  CuboidAxis axis)
{
  static const std::vector<std::pair<int, int>> xEdges = {
    {0, 1}, {3, 2}, {4, 5}, {7, 6},
  };
  static const std::vector<std::pair<int, int>> yEdges = {
    {1, 2}, {0, 3}, {5, 6}, {4, 7},
  };
  static const std::vector<std::pair<int, int>> zEdges = {
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  switch (axis) {
    case CuboidAxis::X: return xEdges;
    case CuboidAxis::Y: return yEdges;
    case CuboidAxis::Z: return zEdges;
  }
  return zEdges;
}

void AppendLinePair(std::vector<MetalLineVertex>& out,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec4& color)
{
  MetalLineVertex va;
  va.pos[0] = a.x;
  va.pos[1] = a.y;
  va.pos[2] = a.z;
  va.pos[3] = 1.0f;
  va.color[0] = color.r;
  va.color[1] = color.g;
  va.color[2] = color.b;
  va.color[3] = color.a;

  MetalLineVertex vb = va;
  vb.pos[0] = b.x;
  vb.pos[1] = b.y;
  vb.pos[2] = b.z;

  out.push_back(va);
  out.push_back(vb);
}

void AppendCuboidEdges(std::vector<MetalLineVertex>& out,
                       const CuboidObject& cuboid,
                       const std::vector<std::pair<int, int>>& edges,
                       const glm::vec4& color)
{
  const std::array<glm::vec3, 8> corners = computeCuboidCorners(cuboid);
  for (const auto& edge : edges) {
    AppendLinePair(out, corners[edge.first], corners[edge.second], color);
  }
}

std::vector<MetalLineVertex> BuildLineVertices(
  const std::vector<LineRenderItem>& lines)
{
  std::vector<MetalLineVertex> vertices;
  for (const LineRenderItem& item : lines) {
    const glm::vec4 color(item.color, item.opacity);
    if (item.mode == LinePrimitiveMode::List) {
      for (std::size_t i = 1; i < item.points.size(); i += 2) {
        AppendLinePair(vertices, item.points[i - 1], item.points[i], color);
      }
    } else {
      for (std::size_t i = 1; i < item.points.size(); ++i) {
        AppendLinePair(vertices, item.points[i - 1], item.points[i], color);
      }
    }
  }
  return vertices;
}

std::vector<MetalLineVertex> BuildCuboidVertices(
  const std::vector<CuboidRenderItem>& cuboids)
{
  std::vector<MetalLineVertex> vertices;
  for (const CuboidRenderItem& item : cuboids) {
    AppendCuboidEdges(vertices,
                      item.cuboid,
                      AllCuboidEdges(),
                      item.cuboid.edgeColor);
    if (item.showHighlight) {
      AppendCuboidEdges(vertices,
                        item.cuboid,
                        SelectedCuboidAxisEdges(item.selectedAxis),
                        item.highlightColor);
    }
  }
  return vertices;
}

std::vector<MetalLineVertex> BuildPolyhedronVertices(
  const std::vector<PolyhedronRenderItem>& polyhedra)
{
  std::vector<MetalLineVertex> vertices;
  for (const PolyhedronRenderItem& item : polyhedra) {
    const glm::vec4 color(item.color, item.opacity);
    for (std::size_t i = 1; i < item.vertices.size(); i += 2) {
      AppendLinePair(vertices, item.vertices[i - 1], item.vertices[i], color);
    }
  }
  return vertices;
}

std::vector<MetalLineVertex> BuildVelocityVertices(
  const std::vector<float>& instances,
  float scaleFactor,
  bool useLogScale)
{
  std::vector<MetalLineVertex> vertices;
  vertices.reserve((instances.size() / 6u) * 6u);
  const glm::vec4 color(1.0f, 0.0f, 0.0f, 1.0f);
  for (std::size_t i = 0; i + 5 < instances.size(); i += 6) {
    const glm::vec3 pos(instances[i + 0], instances[i + 1], instances[i + 2]);
    const glm::vec3 vel(instances[i + 3], instances[i + 4], instances[i + 5]);
    const float speed = glm::length(vel);
    if (speed <= 1.0e-6f) {
      continue;
    }

    const float magnitude = useLogScale ? std::log(speed + 1.0f) : speed;
    const float length = magnitude * scaleFactor;
    if (length <= 0.0f) {
      continue;
    }

    const glm::vec3 dir = vel / speed;
    const glm::vec3 ref =
      std::abs(dir.z) < 0.95f ? glm::vec3(0.0f, 0.0f, 1.0f)
                              : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 side = glm::normalize(glm::cross(ref, dir));
    const glm::vec3 tip = pos + dir * length;
    const glm::vec3 neck = pos + dir * (0.82f * length);
    const glm::vec3 headOffset = side * (0.08f * length);
    AppendLinePair(vertices, pos, tip, color);
    AppendLinePair(vertices, tip, neck + headOffset, color);
    AppendLinePair(vertices, tip, neck - headOffset, color);
  }
  return vertices;
}

struct MetalLineVertexSet {
  id<MTLBuffer> buffer = nil;
  std::size_t count = 0;
  RenderSceneVersion version = 0;
};

struct MetalMesh {
  id<MTLBuffer> vertices = nil;
  id<MTLBuffer> indices = nil;
  std::size_t indexCount = 0;
};

struct MetalInstanceSet {
  id<MTLBuffer> buffer = nil;
  std::size_t count = 0;
  RenderSceneVersion version = 0;
};

struct MetalParticleLodTreeBuffers {
  id<MTLBuffer> nodeCenterRadius = nil;
  id<MTLBuffer> representativePosHsml = nil;
  id<MTLBuffer> representativeValue = nil;
  id<MTLBuffer> nodeMeta = nil;
  id<MTLBuffer> childA = nil;
  id<MTLBuffer> childB = nil;
  id<MTLBuffer> representativeMeta = nil;
  id<MTLBuffer> indices = nil;
  std::size_t nodeCount = 0;
  std::size_t indexCount = 0;
  std::uint32_t maxLeafCount = 1;
  RenderSceneVersion version = 0;
};

#ifdef VOLUME_RENDERING
struct MetalVolumeBuffers {
  id<MTLBuffer> nodeMin = nil;
  id<MTLBuffer> nodeMax = nil;
  id<MTLBuffer> childA = nil;
  id<MTLBuffer> childB = nil;
  id<MTLBuffer> cornerLo = nil;
  id<MTLBuffer> cornerHi = nil;
  std::size_t nodeCount = 0;
  int root = -1;
  RenderSceneVersion version = 0;
};
#endif

struct MetalParticleFrameCache {
  id<MTLTexture> color = nil;
  id<MTLTexture> depth = nil;
  int width = 0;
  int height = 0;
  RenderSceneVersion particlesVersion = 0;
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  ParticleVisualConfig visualConfig;
  bool valid = false;
};

struct MetalGpuParticleLodCache {
  RenderSceneVersion version = 0;
  glm::vec3 cameraPos{0.0f};
  glm::vec3 cameraTarget{0.0f};
  float cameraDistance = 0.0f;
  float focalPx = 0.0f;
  float theta = 0.0f;
  float screenPixelThreshold = 0.0f;
  double lastUpdateTime = -1.0;
  bool valid = false;
};

constexpr std::size_t kParticleGpuLodLevelStatFields = 8;
constexpr std::uint32_t kParticleGpuLodPxScale = 100;

#ifdef VOLUME_RENDERING
struct MetalVolumeFrameCache {
  id<MTLTexture> color = nil;
  int width = 0;
  int height = 0;
  RenderSceneVersion volumeVersion = 0;
  VolumeUniformsCpu uniforms;
  bool uniformInitialized = false;
  bool valid = false;
};
#endif

struct MetalMeshData {
  std::vector<MetalSolidVertex> vertices;
  std::vector<std::uint32_t> indices;
};

void AppendSolidVertex(std::vector<MetalSolidVertex>& vertices,
                       const glm::vec3& p)
{
  MetalSolidVertex v;
  v.pos[0] = p.x;
  v.pos[1] = p.y;
  v.pos[2] = p.z;
  v.pos[3] = 1.0f;
  vertices.push_back(v);
}

MetalMeshData BuildCubeMeshData()
{
  MetalMeshData mesh;
  const glm::vec3 verts[] = {
    {-0.5f, -0.5f, -0.5f},
    { 0.5f, -0.5f, -0.5f},
    { 0.5f,  0.5f, -0.5f},
    {-0.5f,  0.5f, -0.5f},
    {-0.5f, -0.5f,  0.5f},
    { 0.5f, -0.5f,  0.5f},
    { 0.5f,  0.5f,  0.5f},
    {-0.5f,  0.5f,  0.5f}
  };
  for (const glm::vec3& v : verts) {
    AppendSolidVertex(mesh.vertices, v);
  }
  const std::uint32_t idx[] = {
    0,1,2,  2,3,0,
    4,5,6,  6,7,4,
    4,0,3,  3,7,4,
    1,5,6,  6,2,1,
    4,5,1,  1,0,4,
    3,2,6,  6,7,3
  };
  mesh.indices.assign(std::begin(idx), std::end(idx));
  return mesh;
}

MetalMeshData BuildSphereMeshData(int stacks = 32, int slices = 64)
{
  MetalMeshData mesh;
  constexpr float pi = 3.1415926535f;
  for (int i = 0; i <= stacks; ++i) {
    const float v = static_cast<float>(i) / static_cast<float>(stacks);
    const float phi = pi * (v - 0.5f);
    const float z = std::sin(phi);
    const float r = std::cos(phi);
    for (int j = 0; j <= slices; ++j) {
      const float u = static_cast<float>(j) / static_cast<float>(slices);
      const float theta = 2.0f * pi * u;
      AppendSolidVertex(mesh.vertices,
                        glm::vec3(r * std::cos(theta),
                                  r * std::sin(theta),
                                  z));
    }
  }

  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      const std::uint32_t a =
        static_cast<std::uint32_t>(i * (slices + 1) + j);
      const std::uint32_t b =
        static_cast<std::uint32_t>((i + 1) * (slices + 1) + j);
      mesh.indices.insert(mesh.indices.end(),
                          {a, b, b + 1, a, b + 1, a + 1});
    }
  }
  return mesh;
}

MetalMeshData BuildDiskMeshData(int slices = 64)
{
  MetalMeshData mesh;
  AppendSolidVertex(mesh.vertices, {0.0f, 0.5f, 0.0f});
  AppendSolidVertex(mesh.vertices, {0.0f, -0.5f, 0.0f});

  for (int i = 0; i <= slices; ++i) {
    const float th =
      2.0f * glm::pi<float>() * static_cast<float>(i) /
      static_cast<float>(slices);
    const float x = std::cos(th);
    const float z = std::sin(th);
    AppendSolidVertex(mesh.vertices, {x, 0.5f, z});
    AppendSolidVertex(mesh.vertices, {x, -0.5f, z});
  }

  for (int i = 0; i < slices; ++i) {
    mesh.indices.insert(mesh.indices.end(),
                        {0u, 2u + i * 2u, 2u + (i + 1u) * 2u});
    mesh.indices.insert(mesh.indices.end(),
                        {1u, 3u + (i + 1u) * 2u, 3u + i * 2u});
  }

  for (int i = 0; i < slices; ++i) {
    const std::uint32_t a = 2u + i * 2u;
    const std::uint32_t b = a + 1u;
    const std::uint32_t c = 2u + (i + 1u) * 2u;
    const std::uint32_t d = c + 1u;
    mesh.indices.insert(mesh.indices.end(), {a, b, c, c, b, d});
  }
  return mesh;
}

#ifdef ISO_CONTOUR
MetalMeshData BuildIsoContourMeshData(const IsoContourRenderData& data)
{
  MetalMeshData mesh;
  mesh.vertices.reserve(data.verts.size() / 3u);
  for (std::size_t i = 0; i + 2 < data.verts.size(); i += 3) {
    AppendSolidVertex(mesh.vertices,
                      glm::vec3(data.verts[i + 0],
                                data.verts[i + 1],
                                data.verts[i + 2]));
  }
  mesh.indices.reserve(data.inds.size());
  for (unsigned index : data.inds) {
    mesh.indices.push_back(static_cast<std::uint32_t>(index));
  }
  return mesh;
}
#endif

std::vector<MetalSolidInstance> BuildSolidInstances(
  const std::vector<InstancedSolidItem>& items)
{
  std::vector<MetalSolidInstance> instances;
  instances.reserve(items.size());
  for (const InstancedSolidItem& item : items) {
    MetalSolidInstance inst;
    std::memcpy(inst.model, &item.model[0][0], sizeof(float) * 16);
    inst.colorOpacity[0] = item.color.r;
    inst.colorOpacity[1] = item.color.g;
    inst.colorOpacity[2] = item.color.b;
    inst.colorOpacity[3] = item.opacity;
    instances.push_back(inst);
  }
  return instances;
}

MetalSolidInstance BuildSingleSolidInstance(const glm::mat4& model,
                                            const glm::vec3& color,
                                            float opacity)
{
  MetalSolidInstance inst;
  std::memcpy(inst.model, &model[0][0], sizeof(float) * 16);
  inst.colorOpacity[0] = color.r;
  inst.colorOpacity[1] = color.g;
  inst.colorOpacity[2] = color.b;
  inst.colorOpacity[3] = opacity;
  return inst;
}

} // namespace

class MetalRenderBackend final : public RenderBackend {
public:
  explicit MetalRenderBackend(MetalContext& context)
    : context_(&context)
  {
  }

  void init() override
  {
    if (!context_) {
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    if (!device) {
      std::cerr << "Metal render backend: missing MTLDevice." << std::endl;
      return;
    }

    NSError* error = nil;
    NSString* source =
      [NSString stringWithUTF8String:kParticleShaderSource];
    id<MTLLibrary> library =
      [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
      std::cerr << "Metal particle shader compile failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    id<MTLFunction> vertex = [library newFunctionWithName:@"particleVertex"];
    id<MTLFunction> indexedVertex =
      [library newFunctionWithName:@"particleIndexedVertex"];
    id<MTLFunction> rangeVertex =
      [library newFunctionWithName:@"particleRangeVertex"];
    id<MTLFunction> fragment =
      [library newFunctionWithName:@"particleFragment"];
    id<MTLFunction> lineVertex = [library newFunctionWithName:@"lineVertex"];
    id<MTLFunction> lineFragment =
      [library newFunctionWithName:@"lineFragment"];
    id<MTLFunction> solidVertex = [library newFunctionWithName:@"solidVertex"];
    id<MTLFunction> solidFragment =
      [library newFunctionWithName:@"solidFragment"];
    id<MTLFunction> cacheVertex = [library newFunctionWithName:@"cacheVertex"];
    id<MTLFunction> colorCacheFragment =
      [library newFunctionWithName:@"colorCacheFragment"];
    id<MTLFunction> particleCacheFragment =
      [library newFunctionWithName:@"particleCacheFragment"];
    id<MTLFunction> particleLodFrontier =
      [library newFunctionWithName:@"particleLodFrontierKernel"];
    id<MTLFunction> particleLodSinglePass =
      [library newFunctionWithName:@"particleLodSinglePassKernel"];
    id<MTLFunction> particleLodResetQueue =
      [library newFunctionWithName:@"particleLodResetQueueKernel"];
    id<MTLFunction> particleLodWriteDispatchArgs =
      [library newFunctionWithName:@"particleLodWriteDispatchArgsKernel"];
    id<MTLFunction> particleLodFinalize =
      [library newFunctionWithName:@"particleLodFinalizeKernel"];
#ifdef VOLUME_RENDERING
    id<MTLFunction> volumeVertex =
      [library newFunctionWithName:@"volumeVertex"];
    id<MTLFunction> volumeFragment =
      [library newFunctionWithName:@"volumeFragment"];
#endif
    if (!vertex || !indexedVertex || !rangeVertex || !fragment) {
      std::cerr << "Metal particle shader entry point missing." << std::endl;
      return;
    }
    if (!lineVertex || !lineFragment) {
      std::cerr << "Metal line shader entry point missing." << std::endl;
      return;
    }
    if (!solidVertex || !solidFragment) {
      std::cerr << "Metal solid shader entry point missing." << std::endl;
      return;
    }
    if (!cacheVertex || !colorCacheFragment || !particleCacheFragment) {
      std::cerr << "Metal cache shader entry point missing." << std::endl;
      return;
    }
    if (!particleLodFrontier ||
        !particleLodSinglePass ||
        !particleLodResetQueue ||
        !particleLodWriteDispatchArgs ||
        !particleLodFinalize) {
      std::cerr << "Metal particle LOD compute entry point missing."
                << std::endl;
      return;
    }
#ifdef VOLUME_RENDERING
    if (!volumeVertex || !volumeFragment) {
      std::cerr << "Metal volume shader entry point missing." << std::endl;
      return;
    }
#endif

    MTLRenderPipelineDescriptor* desc =
      [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertex;
    desc.fragmentFunction = fragment;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

    particlePipeline_ =
      [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!particlePipeline_) {
      std::cerr << "Metal particle pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    desc.vertexFunction = indexedVertex;
    particleIndexedPipeline_ =
      [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!particleIndexedPipeline_) {
      std::cerr << "Metal indexed particle pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    desc.vertexFunction = rangeVertex;
    particleRangePipeline_ =
      [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!particleRangePipeline_) {
      std::cerr << "Metal particle range pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    MTLRenderPipelineDescriptor* lineDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
    lineDesc.vertexFunction = lineVertex;
    lineDesc.fragmentFunction = lineFragment;
    lineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    lineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    lineDesc.colorAttachments[0].blendingEnabled = YES;
    lineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    lineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    lineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    lineDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

    linePipeline_ =
      [device newRenderPipelineStateWithDescriptor:lineDesc error:&error];
    if (!linePipeline_) {
      std::cerr << "Metal line pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    MTLRenderPipelineDescriptor* solidDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
    solidDesc.vertexFunction = solidVertex;
    solidDesc.fragmentFunction = solidFragment;
    solidDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    solidDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    solidDesc.colorAttachments[0].blendingEnabled = YES;
    solidDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    solidDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    solidDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    solidDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

    solidPipeline_ =
      [device newRenderPipelineStateWithDescriptor:solidDesc error:&error];
    if (!solidPipeline_) {
      std::cerr << "Metal solid pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    MTLRenderPipelineDescriptor* cacheDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
    cacheDesc.vertexFunction = cacheVertex;
    cacheDesc.fragmentFunction = colorCacheFragment;
    cacheDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    cacheDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    cacheDesc.colorAttachments[0].blendingEnabled = YES;
    cacheDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
    cacheDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    cacheDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    cacheDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    colorCachePipeline_ =
      [device newRenderPipelineStateWithDescriptor:cacheDesc error:&error];
    if (!colorCachePipeline_) {
      std::cerr << "Metal color cache pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    MTLRenderPipelineDescriptor* particleCacheDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
    particleCacheDesc.vertexFunction = cacheVertex;
    particleCacheDesc.fragmentFunction = particleCacheFragment;
    particleCacheDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    particleCacheDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    particleCacheDesc.colorAttachments[0].blendingEnabled = YES;
    particleCacheDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
    particleCacheDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    particleCacheDesc.colorAttachments[0].sourceAlphaBlendFactor =
      MTLBlendFactorOne;
    particleCacheDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    particleCachePipeline_ =
      [device newRenderPipelineStateWithDescriptor:particleCacheDesc
                                             error:&error];
    if (!particleCachePipeline_) {
      std::cerr << "Metal particle cache pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }

    particleLodFrontierPipeline_ =
      [device newComputePipelineStateWithFunction:particleLodFrontier
                                            error:&error];
    if (!particleLodFrontierPipeline_) {
      std::cerr << "Metal particle LOD frontier pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
    particleLodSinglePassPipeline_ =
      [device newComputePipelineStateWithFunction:particleLodSinglePass
                                            error:&error];
    if (!particleLodSinglePassPipeline_) {
      std::cerr << "Metal particle LOD single-pass pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
    particleLodResetQueuePipeline_ =
      [device newComputePipelineStateWithFunction:particleLodResetQueue
                                            error:&error];
    if (!particleLodResetQueuePipeline_) {
      std::cerr << "Metal particle LOD queue reset pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
    particleLodDispatchArgsPipeline_ =
      [device newComputePipelineStateWithFunction:particleLodWriteDispatchArgs
                                            error:&error];
    if (!particleLodDispatchArgsPipeline_) {
      std::cerr << "Metal particle LOD dispatch args pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
    particleLodFinalizePipeline_ =
      [device newComputePipelineStateWithFunction:particleLodFinalize
                                            error:&error];
    if (!particleLodFinalizePipeline_) {
      std::cerr << "Metal particle LOD finalize pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
    experimentalGpuParticleLod_ =
      !EnvFlagDisabled("PARTICLE_VIS_GPU_PARTICLE_LOD") &&
      !EnvFlagDisabled("PARTICLE_VIS_METAL_GPU_LOD");
    if (experimentalGpuParticleLod_) {
      std::cerr << "Metal experimental GPU particle LOD is enabled."
                << std::endl;
    }

#ifdef VOLUME_RENDERING
    MTLRenderPipelineDescriptor* volumeDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
    volumeDesc.vertexFunction = volumeVertex;
    volumeDesc.fragmentFunction = volumeFragment;
    volumeDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    volumeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    volumeDesc.colorAttachments[0].blendingEnabled = YES;
    volumeDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
    volumeDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    volumeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    volumeDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    volumePipeline_ =
      [device newRenderPipelineStateWithDescriptor:volumeDesc error:&error];
    if (!volumePipeline_) {
      std::cerr << "Metal volume pipeline creation failed: "
                << (error ? [[error localizedDescription] UTF8String]
                          : "unknown error")
                << std::endl;
      return;
    }
#endif

    MTLDepthStencilDescriptor* depthDesc =
      [[MTLDepthStencilDescriptor alloc] init];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;
    depthStencil_ = [device newDepthStencilStateWithDescriptor:depthDesc];
    if (!depthStencil_) {
      std::cerr << "Metal particle depth state creation failed." << std::endl;
      return;
    }
    MTLDepthStencilDescriptor* lineDepthDesc =
      [[MTLDepthStencilDescriptor alloc] init];
    lineDepthDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
    lineDepthDesc.depthWriteEnabled = NO;
    lineDepthStencil_ =
      [device newDepthStencilStateWithDescriptor:lineDepthDesc];
    if (!lineDepthStencil_) {
      std::cerr << "Metal line depth state creation failed." << std::endl;
      return;
    }

    MTLDepthStencilDescriptor* cacheDepthDesc =
      [[MTLDepthStencilDescriptor alloc] init];
    cacheDepthDesc.depthCompareFunction = MTLCompareFunctionAlways;
    cacheDepthDesc.depthWriteEnabled = YES;
    cacheDepthStencil_ =
      [device newDepthStencilStateWithDescriptor:cacheDepthDesc];
    if (!cacheDepthStencil_) {
      std::cerr << "Metal cache depth state creation failed." << std::endl;
      return;
    }

#ifdef VOLUME_RENDERING
    MTLDepthStencilDescriptor* volumeDepthDesc =
      [[MTLDepthStencilDescriptor alloc] init];
    volumeDepthDesc.depthCompareFunction = MTLCompareFunctionAlways;
    volumeDepthDesc.depthWriteEnabled = NO;
    volumeDepthStencil_ =
      [device newDepthStencilStateWithDescriptor:volumeDepthDesc];
    if (!volumeDepthStencil_) {
      std::cerr << "Metal volume depth state creation failed." << std::endl;
      return;
    }
#endif

    if (!createColormapResources(device)) {
      return;
    }
    createSolidMeshes(device);

    initialized_ = true;
    std::cerr << "Metal render backend: particle point rendering initialized."
              << std::endl;
  }

  void destroy() override
  {
    if (context_) {
      context_->setPreImGuiDrawCallback({});
    }
    particleBuffer_ = nil;
    stressParticleBuffer_ = nil;
    particleLodOrderedBuffer_ = nil;
    particleLodBuffer_ = nil;
    stressParticleLodBuffer_ = nil;
    particleLodTreeBuffers_ = MetalParticleLodTreeBuffers{};
    particleLodDrawRanges_.clear();
    particleLodRangeIcb_ = nil;
    particleLodRetiredRangeIcbs_.clear();
    particleLodRangeIcbCapacity_ = 0;
    particleLodRangeIcbCommandCount_ = 0;
    particleLodRangeIcbValid_ = false;
    particleLodNormalDrawFallback_ = false;
    particleLodNeedsIcbBuild_ = false;
    particleLodRangeDataReady_.store(false);
    particlePipeline_ = nil;
    particleIndexedPipeline_ = nil;
    particleRangePipeline_ = nil;
    linePipeline_ = nil;
    solidPipeline_ = nil;
    colorCachePipeline_ = nil;
    particleCachePipeline_ = nil;
    particleLodFrontierPipeline_ = nil;
    particleLodSinglePassPipeline_ = nil;
    particleLodResetQueuePipeline_ = nil;
    particleLodDispatchArgsPipeline_ = nil;
    particleLodFinalizePipeline_ = nil;
    gpuParticleLodBuffer_ = nil;
    gpuStressParticleLodBuffer_ = nil;
    gpuLeafRangeLodBuffer_ = nil;
    gpuParticleLodNodeFlagBuffer_ = nil;
    gpuParticleLodLevelStatsBuffer_ = nil;
    gpuParticleLodCounterBuffer_ = nil;
    gpuParticleLodIndirectBuffer_ = nil;
    gpuParticleLodQueueA_ = nil;
    gpuParticleLodQueueB_ = nil;
    gpuParticleLodQueueCountA_ = nil;
    gpuParticleLodQueueCountB_ = nil;
    gpuParticleLodDispatchArgsA_ = nil;
    gpuParticleLodDispatchArgsB_ = nil;
    gpuParticleLodCache_ = MetalGpuParticleLodCache{};
#ifdef VOLUME_RENDERING
    volumePipeline_ = nil;
#endif
    depthStencil_ = nil;
    lineDepthStencil_ = nil;
    cacheDepthStencil_ = nil;
#ifdef VOLUME_RENDERING
    volumeDepthStencil_ = nil;
#endif
    colormapTexture_ = nil;
    colormapSampler_ = nil;
    previewTexture_ = nil;
    preview_ = ProjectionPreviewUIState{};
    previewVersion_ = 0;
    lineVertices_ = MetalLineVertexSet{};
    cuboidVertices_ = MetalLineVertexSet{};
    polyhedronVertices_ = MetalLineVertexSet{};
    velocityVertices_ = MetalLineVertexSet{};
    cubeInstances_ = MetalInstanceSet{};
    diskInstances_ = MetalInstanceSet{};
    ellipsoidInstances_ = MetalInstanceSet{};
    particleFrameCache_ = MetalParticleFrameCache{};
#ifdef VOLUME_RENDERING
    volumeBuffers_ = MetalVolumeBuffers{};
    volumeFrameCache_ = MetalVolumeFrameCache{};
#endif
#ifdef ISO_CONTOUR
    isoContourMesh_ = MetalMesh{};
    isoContourInstances_ = MetalInstanceSet{};
    isoContourVersion_ = 0;
#endif
    cubeMesh_ = MetalMesh{};
    diskMesh_ = MetalMesh{};
    sphereMesh_ = MetalMesh{};
    particleCount_ = 0;
    stressParticleCount_ = 0;
    particleLodOrderedCount_ = 0;
    particleLodCount_ = 0;
    stressParticleLodCount_ = 0;
    gpuParticleLodCount_ = 0;
    gpuStressParticleLodCount_ = 0;
    gpuParticleLodCapacity_ = 0;
    gpuStressParticleLodCapacity_ = 0;
    gpuLeafRangeLodCapacity_ = 0;
    gpuParticleLodNodeFlagCapacity_ = 0;
    gpuParticleLodQueueCapacity_ = 0;
    particleVersion_ = 0;
    stressParticleVersion_ = 0;
    particleLodOrderedVersion_ = 0;
    particleLodVersion_ = 0;
    stressParticleLodVersion_ = 0;
    gpuParticleLodMismatchVersion_ = 0;
    velocityVersion_ = 0;
    velocityArrowScale_ = 0.0f;
    velocityUseLogScale_ = false;
    initialized_ = false;
  }

  void render(const RenderFrameState& frame,
              const RenderSceneData& scene) override
  {
    if (!initialized_ || !context_ || !frame.valid) {
      return;
    }
    syncParticles(scene);
    syncStressParticles(scene);
    useParticleLod_ =
      ShouldUseParticleLod(frame.runtime,
                           scene.particleLodProxy,
                           false,
                           experimentalGpuParticleLod_ &&
                             scene.particleLodGpu.valid);
    if (useParticleLod_) {
      syncParticleBuffer(scene.particleLodOrderedParticles,
                         scene.particleLodVersion,
                         particleLodOrderedBuffer_,
                         particleLodOrderedVersion_,
                         particleLodOrderedCount_);
      syncParticleBuffer(scene.particleLodProxy,
                         scene.particleLodVersion,
                         particleLodBuffer_,
                         particleLodVersion_,
                         particleLodCount_);
      syncParticleBuffer(scene.particleLodStressProxy,
                         scene.particleLodVersion,
                         stressParticleLodBuffer_,
                         stressParticleLodVersion_,
                         stressParticleLodCount_);
      syncParticleLodTree(scene);
    }
#ifdef VOLUME_RENDERING
    syncVolume(scene);
#endif
    syncLineVertices(scene, frame.runtime.velocity);
    syncSolidInstances(scene);
#ifdef ISO_CONTOUR
    syncIsoContour(scene);
#endif

    MTLViewport viewport{
      static_cast<double>(frame.viewport.x),
      static_cast<double>(frame.viewport.y),
      static_cast<double>(frame.viewport.width),
      static_cast<double>(frame.viewport.height),
      0.0,
      1.0
    };
    id<MTLRenderCommandEncoder> encoder = currentEncoder();
    if (!encoder) {
      return;
    }
    [encoder setViewport:viewport];
    timing_.particleDrawActive = true;
    timing_.particleDrawCacheHit = false;
    bool particleDrawMeasured = false;
    bool particleLodDrawMeasured = false;
    const bool gpuLodActive = useParticleLod_ &&
                              experimentalGpuParticleLod_ &&
                              scene.particleLodGpu.valid;
    bool gpuLodReady = false;
    if (gpuLodActive) {
      gpuLodReady = encodeGpuParticleLodProxy(frame, scene, false);
      encoder = currentEncoder();
      if (!encoder) {
        return;
      }
      [encoder setViewport:viewport];
    }
    const bool lodParticlesOnlyDebug =
      useParticleLod_ && EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_PARTICLES_ONLY");
#ifdef VOLUME_RENDERING
    const bool skipVolumeForLodDebug =
      lodParticlesOnlyDebug ||
      (useParticleLod_ && EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_DISABLE_VOLUME"));
    if (skipVolumeForLodDebug) {
      volumeFrameCache_.valid = false;
    } else {
      updateVolumeFrameCache(frame);
      encoder = currentEncoder();
      if (!encoder) {
        return;
      }
      [encoder setViewport:viewport];
      if (frame.runtime.scheduling.cacheVolumeFrames) {
        if (!drawCachedVolumeFrame(encoder, frame)) {
          drawVolume(encoder, frame, 1.0f);
        }
      } else {
        volumeFrameCache_.valid = false;
        drawVolume(encoder, frame, 1.0f);
      }
    }
#endif

    std::chrono::steady_clock::time_point particleDrawStart;
    auto beginParticleDrawTiming = [&]() {
      particleDrawStart = std::chrono::steady_clock::now();
    };
    if (useParticleLod_) {
      particleFrameCache_.valid = false;
      if (gpuLodReady) {
        encoder = currentEncoder();
        if (!encoder) {
          return;
        }
        [encoder setViewport:viewport];
        beginParticleDrawTiming();
        particleLodDrawMeasured = drawParticleLodIcb(encoder, frame);
        if (!particleLodDrawMeasured) {
          drawParticles(encoder, frame);
          particleLodDrawMeasured = true;
        }
      } else if (gpuLodActive) {
        encoder = currentEncoder();
        if (encoder) {
          [encoder setViewport:viewport];
          beginParticleDrawTiming();
          particleLodDrawMeasured = drawParticleLodIcb(encoder, frame);
          if (!particleLodDrawMeasured) {
            drawParticles(encoder, frame);
            particleLodDrawMeasured = true;
          }
        }
      } else if (!scene.particleLodProxy.empty()) {
        beginParticleDrawTiming();
        drawParticleBuffer(encoder,
                           particleLodBuffer_,
                           particleLodCount_,
                           frame);
        drawParticleBuffer(encoder,
                           stressParticleLodBuffer_,
                           stressParticleLodCount_,
                           frame);
        particleDrawMeasured = true;
      } else {
        beginParticleDrawTiming();
        drawParticles(encoder, frame);
        particleDrawMeasured = true;
      }
    } else if (frame.runtime.scheduling.cacheParticleFrames &&
               !frame.runtime.scheduling.interactionActive) {
      updateParticleFrameCache(frame);
      encoder = currentEncoder();
      if (!encoder) {
        return;
      }
      [encoder setViewport:viewport];
      if (!drawParticleFrameCache(encoder)) {
        beginParticleDrawTiming();
        drawParticles(encoder, frame);
        particleDrawMeasured = true;
      } else {
        timing_.particleDrawCacheHit = true;
      }
      drawParticleBuffer(encoder,
                         stressParticleBuffer_,
                         stressParticleCount_,
                         frame);
    } else {
      particleFrameCache_.valid = false;
      beginParticleDrawTiming();
      drawParticles(encoder, frame);
      drawParticleBuffer(encoder,
                         stressParticleBuffer_,
                         stressParticleCount_,
                         frame);
      particleDrawMeasured = true;
    }
    if (particleDrawMeasured) {
      const auto particleDrawEnd = std::chrono::steady_clock::now();
      timing_.particleDrawWallMs =
        std::chrono::duration<double, std::milli>(
          particleDrawEnd - particleDrawStart).count();
      timing_.particleDrawWallTimeKnown = true;
      timing_.particleDrawRefreshHz =
        timing_.particleDrawWallMs > 0.0
          ? 1000.0 / timing_.particleDrawWallMs
          : 0.0;
    }
    if (particleLodDrawMeasured) {
      const auto particleDrawEnd = std::chrono::steady_clock::now();
      timing_.particleGpuLodDrawWallMs =
        std::chrono::duration<double, std::milli>(
          particleDrawEnd - particleDrawStart).count();
      timing_.particleGpuLodDrawWallTimeKnown = true;
      timing_.particleGpuLodDrawRefreshHz =
        timing_.particleGpuLodDrawWallMs > 0.0
          ? 1000.0 / timing_.particleGpuLodDrawWallMs
          : 0.0;
    }

    if (!lodParticlesOnlyDebug) {
      drawLineSet(encoder, velocityVertices_, frame, frame.runtime.velocity);
      drawLineSet(encoder, cuboidVertices_, frame, frame.runtime.cuboids);
      drawLineSet(encoder, lineVertices_, frame, frame.runtime.lines);
      drawLineSet(encoder, polyhedronVertices_, frame, frame.runtime.polyhedra);
      drawSolidSet(encoder, cubeMesh_, cubeInstances_, frame, frame.runtime.cubes);
      drawSolidSet(encoder, diskMesh_, diskInstances_, frame, frame.runtime.disks);
      drawSolidSet(encoder,
                   sphereMesh_,
                   ellipsoidInstances_,
                   frame,
                   frame.runtime.ellipsoids);
    }
#ifdef ISO_CONTOUR
    drawSolidSet(encoder,
                 isoContourMesh_,
                 isoContourInstances_,
                 frame,
                 frame.runtime.isocontour);
#endif

    frame.overlay.particleLabels.draw(frame.matrices.view,
                                      frame.matrices.projection,
                                      frame.viewport);
    DrawColorbarOverlay(frame);
    DrawGizmoOverlays(frame);
  }

  void updateProjectionPreview(const RgbImage& image) override
  {
    uploadProjectionPreview(image);
  }

  ProjectionPreviewUIState makeProjectionPreviewUIState() const override
  {
    return preview_;
  }

  RenderBackendCapabilities capabilities() const override
  {
    RenderBackendCapabilities caps;
    caps.particles = initialized_;
    caps.particleLod = initialized_;
    caps.particleGpuLod = initialized_ && experimentalGpuParticleLod_;
    caps.velocityField = initialized_;
    caps.instancedObjects = initialized_;
    caps.lines = initialized_;
    caps.polyhedra = initialized_;
    caps.colorbar = initialized_;
    caps.gizmos = initialized_;
    caps.projectionPreview = initialized_;
    caps.particleFrameCache = initialized_;
    caps.gpuMemoryQuery = initialized_;
#ifdef VOLUME_RENDERING
    caps.volumeRendering = initialized_;
    caps.volumeFrameCache = initialized_;
#endif
#ifdef ISO_CONTOUR
    caps.isoContour = initialized_;
#endif
    return caps;
  }

  RenderBackendMemoryInfo queryMemoryInfo() const override
  {
    RenderBackendMemoryInfo info;
    id<MTLDevice> device =
      context_ ? (__bridge id<MTLDevice>)context_->device() : nil;
    if (!device) {
      return info;
    }

    const unsigned long long recommended = [device recommendedMaxWorkingSetSize];
    const unsigned long long allocated = [device currentAllocatedSize];
    if (recommended > 0 && recommended > allocated) {
      info.gpuAvailableKnown = true;
      info.gpuAvailableBytes =
        static_cast<std::size_t>(recommended - allocated);
    }
    return info;
  }

  RenderBackendTimingInfo queryTimingInfo() const override
  {
    return timing_;
  }

private:
  void uploadProjectionPreview(const RgbImage& image)
  {
    if (!image.valid()) {
      return;
    }
    if (previewVersion_ == image.version && preview_.valid) {
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    if (!device) {
      return;
    }

    const bool sizeChanged =
      image.width != preview_.width || image.height != preview_.height ||
      !previewTexture_;
    if (sizeChanged) {
      MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(image.width)
                                                          height:static_cast<NSUInteger>(image.height)
                                                       mipmapped:NO];
      desc.usage = MTLTextureUsageShaderRead;
      desc.storageMode = MTLStorageModeManaged;
      previewTexture_ = [device newTextureWithDescriptor:desc];
      if (!previewTexture_) {
        preview_ = ProjectionPreviewUIState{};
        return;
      }
    }

    std::vector<unsigned char> rgba(
      static_cast<std::size_t>(image.width) *
      static_cast<std::size_t>(image.height) * 4u);
    for (std::size_t src = 0, dst = 0; src + 2 < image.rgb.size();
         src += 3, dst += 4) {
      rgba[dst + 0] = image.rgb[src + 0];
      rgba[dst + 1] = image.rgb[src + 1];
      rgba[dst + 2] = image.rgb[src + 2];
      rgba[dst + 3] = 255;
    }

    MTLRegion region =
      MTLRegionMake2D(0, 0, image.width, image.height);
    [previewTexture_ replaceRegion:region
                        mipmapLevel:0
                          withBytes:rgba.data()
                        bytesPerRow:image.width * 4u];

    preview_.textureId = (__bridge void*)previewTexture_;
    preview_.width = image.width;
    preview_.height = image.height;
    preview_.version = image.version;
    preview_.valid = true;
    previewVersion_ = image.version;
  }

  bool createColormapResources(id<MTLDevice> device)
  {
    constexpr int kSamples = 256;
    const int rows = std::max(1, gNumColormaps);
    std::vector<float> pixels = BuildColormapTexturePixels();

    MTLTextureDescriptor* texDesc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                         width:kSamples
                                                        height:rows
                                                     mipmapped:NO];
    texDesc.usage = MTLTextureUsageShaderRead;
    texDesc.storageMode = MTLStorageModeManaged;
    colormapTexture_ = [device newTextureWithDescriptor:texDesc];
    if (!colormapTexture_) {
      std::cerr << "Metal colormap texture creation failed." << std::endl;
      return false;
    }

    MTLRegion region = MTLRegionMake2D(0, 0, kSamples, rows);
    [colormapTexture_ replaceRegion:region
                         mipmapLevel:0
                           withBytes:pixels.data()
                         bytesPerRow:kSamples * 4 * sizeof(float)];

    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    colormapSampler_ = [device newSamplerStateWithDescriptor:samplerDesc];
    if (!colormapSampler_) {
      std::cerr << "Metal colormap sampler creation failed." << std::endl;
      return false;
    }
    return true;
  }

  void uploadMesh(id<MTLDevice> device,
                  const MetalMeshData& data,
                  MetalMesh& mesh)
  {
    mesh = MetalMesh{};
    if (data.vertices.empty() || data.indices.empty()) {
      return;
    }
    mesh.vertices =
      [device newBufferWithBytes:data.vertices.data()
                          length:data.vertices.size() * sizeof(MetalSolidVertex)
                         options:MTLResourceStorageModeShared];
    mesh.indices =
      [device newBufferWithBytes:data.indices.data()
                          length:data.indices.size() * sizeof(std::uint32_t)
                         options:MTLResourceStorageModeShared];
    mesh.indexCount = data.indices.size();
  }

  void createSolidMeshes(id<MTLDevice> device)
  {
    uploadMesh(device, BuildCubeMeshData(), cubeMesh_);
    uploadMesh(device, BuildDiskMeshData(), diskMesh_);
    uploadMesh(device, BuildSphereMeshData(), sphereMesh_);
  }

  void syncInstanceSet(const std::vector<InstancedSolidItem>& items,
                       RenderSceneVersion version,
                       MetalInstanceSet& set)
  {
    if (set.version == version && set.buffer) {
      set.count = items.size();
      return;
    }
    set.version = version;
    set.count = items.size();
    if (items.empty()) {
      set.buffer = nil;
      return;
    }

    const std::vector<MetalSolidInstance> instances =
      BuildSolidInstances(items);
    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    set.buffer =
      [device newBufferWithBytes:instances.data()
                          length:instances.size() * sizeof(MetalSolidInstance)
                         options:MTLResourceStorageModeShared];
  }

  void syncSolidInstances(const RenderSceneData& scene)
  {
    syncInstanceSet(scene.cubes, scene.cubesVersion, cubeInstances_);
    syncInstanceSet(scene.disks, scene.disksVersion, diskInstances_);
    syncInstanceSet(scene.ellipsoids,
                    scene.ellipsoidsVersion,
                    ellipsoidInstances_);
  }

#ifdef ISO_CONTOUR
  void syncIsoContour(const RenderSceneData& scene)
  {
    if (isoContourVersion_ == scene.isoContourVersion) {
      return;
    }
    isoContourVersion_ = scene.isoContourVersion;

    const MetalMeshData meshData = BuildIsoContourMeshData(scene.isoContour);
    if (meshData.vertices.empty() || meshData.indices.empty()) {
      isoContourMesh_ = MetalMesh{};
      isoContourInstances_ = MetalInstanceSet{};
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    uploadMesh(device, meshData, isoContourMesh_);

    const MetalSolidInstance instance =
      BuildSingleSolidInstance(glm::mat4(1.0f),
                               glm::vec3(1.0f),
                               1.0f);
    isoContourInstances_.buffer =
      [device newBufferWithBytes:&instance
                          length:sizeof(instance)
                         options:MTLResourceStorageModeShared];
    isoContourInstances_.count = 1;
    isoContourInstances_.version = scene.isoContourVersion;
  }
#endif

  void syncLineVertexSet(const std::vector<MetalLineVertex>& vertices,
                         RenderSceneVersion version,
                         MetalLineVertexSet& set)
  {
    if (set.version == version && set.buffer) {
      set.count = vertices.size();
      return;
    }

    set.version = version;
    set.count = vertices.size();
    if (vertices.empty()) {
      set.buffer = nil;
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    set.buffer =
      [device newBufferWithBytes:vertices.data()
                          length:vertices.size() * sizeof(MetalLineVertex)
                         options:MTLResourceStorageModeShared];
  }

  void syncVelocityVertices(const RenderSceneData& scene,
                            const VelocityRenderState& runtime)
  {
    if (velocityVersion_ == scene.velocityVersion &&
        velocityArrowScale_ == runtime.arrowScale &&
        velocityUseLogScale_ == runtime.useLogScale &&
        velocityVertices_.buffer) {
      return;
    }

    const std::vector<MetalLineVertex> vertices =
      BuildVelocityVertices(scene.velocityInstances,
                            runtime.arrowScale,
                            runtime.useLogScale);
    syncLineVertexSet(vertices,
                      scene.velocityVersion,
                      velocityVertices_);
    velocityVersion_ = scene.velocityVersion;
    velocityArrowScale_ = runtime.arrowScale;
    velocityUseLogScale_ = runtime.useLogScale;
  }

  void syncLineVertices(const RenderSceneData& scene,
                        const VelocityRenderState& velocity)
  {
    syncLineVertexSet(BuildLineVertices(scene.lines),
                      scene.linesVersion,
                      lineVertices_);
    syncLineVertexSet(BuildCuboidVertices(scene.cuboids),
                      scene.cuboidsVersion,
                      cuboidVertices_);
    syncLineVertexSet(BuildPolyhedronVertices(scene.polyhedra),
                      scene.polyhedraVersion,
                      polyhedronVertices_);
    syncVelocityVertices(scene, velocity);
  }

  void drawLineSet(id<MTLRenderCommandEncoder> encoder,
                   const MetalLineVertexSet& vertices,
                   const RenderFrameState& frame,
                   const RenderLayerState& runtime)
  {
    if (!runtime.show || vertices.count == 0 || !vertices.buffer ||
        !linePipeline_) {
      return;
    }

    LineUniformsCpu uniforms;
    FillLineUniforms(frame, runtime.opacity, uniforms);

    [encoder setRenderPipelineState:linePipeline_];
    [encoder setDepthStencilState:lineDepthStencil_];
    [encoder setVertexBuffer:vertices.buffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeLine
                vertexStart:0
                vertexCount:vertices.count];
  }

  void drawSolidSet(id<MTLRenderCommandEncoder> encoder,
                    const MetalMesh& mesh,
                    const MetalInstanceSet& instances,
                    const RenderFrameState& frame,
                    const RenderLayerState& runtime)
  {
    if (!runtime.show || instances.count == 0 || mesh.indexCount == 0 ||
        !mesh.vertices || !mesh.indices || !instances.buffer ||
        !solidPipeline_) {
      return;
    }

    SolidUniformsCpu uniforms;
    FillSolidUniforms(frame, runtime.opacity, uniforms);

    [encoder setRenderPipelineState:solidPipeline_];
    [encoder setDepthStencilState:lineDepthStencil_];
    [encoder setVertexBuffer:mesh.vertices offset:0 atIndex:0];
    [encoder setVertexBuffer:instances.buffer offset:0 atIndex:1];
    [encoder setVertexBytes:&uniforms
                     length:sizeof(uniforms)
                    atIndex:2];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:mesh.indexCount
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:mesh.indices
                 indexBufferOffset:0
                     instanceCount:instances.count];
  }

  id<MTLRenderCommandEncoder> currentEncoder() const
  {
    return (__bridge id<MTLRenderCommandEncoder>)
      context_->currentRenderCommandEncoder();
  }

  id<MTLCommandBuffer> currentCommandBuffer() const
  {
    return (__bridge id<MTLCommandBuffer>)context_->currentCommandBuffer();
  }

  bool ensureTexture(id<MTLTexture>& texture,
                     MTLPixelFormat format,
                     int width,
                     int height,
                     MTLTextureUsage usage)
  {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (texture &&
        static_cast<int>(texture.width) == width &&
        static_cast<int>(texture.height) == height &&
        texture.pixelFormat == format) {
      return true;
    }
    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    if (!device) {
      texture = nil;
      return false;
    }
    MTLTextureDescriptor* desc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                         width:static_cast<NSUInteger>(width)
                                                        height:static_cast<NSUInteger>(height)
                                                     mipmapped:NO];
    desc.usage = usage;
    desc.storageMode = MTLStorageModePrivate;
    texture = [device newTextureWithDescriptor:desc];
    return texture != nil;
  }

  MTLRenderPassDescriptor* makeOffscreenPass(id<MTLTexture> color,
                                             id<MTLTexture> depth)
  {
    if (!color) {
      return nil;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    auto* colorAttachment = pass.colorAttachments[0];
    colorAttachment.texture = color;
    colorAttachment.loadAction = MTLLoadActionClear;
    colorAttachment.storeAction = MTLStoreActionStore;
    colorAttachment.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    if (depth) {
      auto* depthAttachment = pass.depthAttachment;
      depthAttachment.texture = depth;
      depthAttachment.loadAction = MTLLoadActionClear;
      depthAttachment.storeAction = MTLStoreActionStore;
      depthAttachment.clearDepth = 1.0;
    }
    return pass;
  }

  void drawParticles(id<MTLRenderCommandEncoder> encoder,
                     const RenderFrameState& frame)
  {
    drawParticleBuffer(encoder, particleBuffer_, particleCount_, frame);
  }

  void drawParticleBuffer(id<MTLRenderCommandEncoder> encoder,
                          id<MTLBuffer> buffer,
                          std::size_t count,
                          const RenderFrameState& frame)
  {
    if (!particlePipeline_ || !buffer || count == 0) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    [encoder setRenderPipelineState:particlePipeline_];
    [encoder setDepthStencilState:depthStencil_];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
                vertexStart:0
                vertexCount:count];
  }

  void drawParticleBufferRange(id<MTLRenderCommandEncoder> encoder,
                               id<MTLBuffer> buffer,
                               std::size_t start,
                               std::size_t count,
                               const RenderFrameState& frame)
  {
    if (!particlePipeline_ || !buffer || count == 0) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    [encoder setRenderPipelineState:particlePipeline_];
    [encoder setDepthStencilState:depthStencil_];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
                vertexStart:start
                vertexCount:count];
  }

  void drawParticleBufferIndirect(id<MTLRenderCommandEncoder> encoder,
                                  id<MTLBuffer> buffer,
                                  id<MTLBuffer> indirectBuffer,
                                  NSUInteger indirectOffset,
                                  const RenderFrameState& frame)
  {
    if (!particlePipeline_ || !buffer || !indirectBuffer) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    [encoder setRenderPipelineState:particlePipeline_];
    [encoder setDepthStencilState:depthStencil_];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
             indirectBuffer:indirectBuffer
       indirectBufferOffset:indirectOffset];
  }

  void drawParticleBufferIndexedIndirect(id<MTLRenderCommandEncoder> encoder,
                                         id<MTLBuffer> particleBuffer,
                                         id<MTLBuffer> indexBuffer,
                                         id<MTLBuffer> indirectBuffer,
                                         NSUInteger indirectOffset,
                                         const RenderFrameState& frame)
  {
    if (!particleIndexedPipeline_ || !particleBuffer || !indexBuffer ||
        !indirectBuffer) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    if (EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_FIXED_COLOR")) {
      uniforms.misc[3] = 1.0f;
    }
    [encoder setRenderPipelineState:particleIndexedPipeline_];
    [encoder setDepthStencilState:
               EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_DEPTH_ALWAYS")
                 ? cacheDepthStencil_
                 : depthStencil_];
    [encoder setVertexBuffer:particleBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setVertexBuffer:indexBuffer offset:0 atIndex:2];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
             indirectBuffer:indirectBuffer
       indirectBufferOffset:indirectOffset];
  }

  void drawParticleBufferIndexedRange(id<MTLRenderCommandEncoder> encoder,
                                      id<MTLBuffer> particleBuffer,
                                      id<MTLBuffer> indexBuffer,
                                      std::size_t indexStart,
                                      std::size_t count,
                                      const RenderFrameState& frame)
  {
    if (!particleIndexedPipeline_ || !particleBuffer || !indexBuffer ||
        count == 0) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    if (EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_FIXED_COLOR")) {
      uniforms.misc[3] = 1.0f;
    }
    [encoder setRenderPipelineState:particleIndexedPipeline_];
    [encoder setDepthStencilState:
               EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_DEPTH_ALWAYS")
                 ? cacheDepthStencil_
                 : depthStencil_];
    [encoder setVertexBuffer:particleBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setVertexBuffer:indexBuffer
                       offset:indexStart * sizeof(std::uint32_t)
                      atIndex:2];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
                vertexStart:0
                vertexCount:count];
  }

  void drawParticleRangeBufferIndirect(id<MTLRenderCommandEncoder> encoder,
                                       id<MTLBuffer> orderedParticleBuffer,
                                       id<MTLBuffer> leafRangeBuffer,
                                       std::uint32_t maxLeafCount,
                                       id<MTLBuffer> indirectBuffer,
                                       NSUInteger indirectOffset,
                                       const RenderFrameState& frame)
  {
    if (!particleRangePipeline_ || !orderedParticleBuffer || !leafRangeBuffer ||
        !indirectBuffer || maxLeafCount == 0) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);
    ParticleRangeUniformsCpu rangeUniforms;
    rangeUniforms.maxLeafCount = maxLeafCount;

    [encoder setRenderPipelineState:particleRangePipeline_];
    [encoder setDepthStencilState:depthStencil_];
    [encoder setVertexBuffer:orderedParticleBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setVertexBuffer:leafRangeBuffer offset:0 atIndex:2];
    [encoder setVertexBytes:&rangeUniforms
                     length:sizeof(rangeUniforms)
                    atIndex:3];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypePoint
             indirectBuffer:indirectBuffer
       indirectBufferOffset:indirectOffset];
  }

  bool particleFrameCacheMatches(const RenderFrameState& frame) const
  {
    return particleFrameCache_.valid &&
           particleFrameCache_.particlesVersion == particleVersion_ &&
           particleFrameCache_.width == frame.viewport.width &&
           particleFrameCache_.height == frame.viewport.height &&
           EqualMatrix(particleFrameCache_.model, frame.matrices.model) &&
           EqualMatrix(particleFrameCache_.view, frame.matrices.view) &&
           EqualMatrix(particleFrameCache_.projection,
                       frame.matrices.projection) &&
           EqualParticleVisualConfig(particleFrameCache_.visualConfig,
                                     frame.particleVisual);
  }

  void updateParticleFrameCache(const RenderFrameState& frame)
  {
    if (!particlePipeline_ || !particleBuffer_ || particleCount_ == 0 ||
        particleFrameCacheMatches(frame)) {
      return;
    }

    const int width = std::max(frame.viewport.width, 1);
    const int height = std::max(frame.viewport.height, 1);
    const bool colorOk =
      ensureTexture(particleFrameCache_.color,
                    MTLPixelFormatBGRA8Unorm,
                    width,
                    height,
                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    const bool depthOk =
      ensureTexture(particleFrameCache_.depth,
                    MTLPixelFormatDepth32Float,
                    width,
                    height,
                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    if (!colorOk || !depthOk) {
      particleFrameCache_.valid = false;
      return;
    }

    id<MTLCommandBuffer> commandBuffer = currentCommandBuffer();
    if (!commandBuffer) {
      particleFrameCache_.valid = false;
      return;
    }
    context_->endCurrentRenderCommandEncoder();

    MTLRenderPassDescriptor* pass =
      makeOffscreenPass(particleFrameCache_.color, particleFrameCache_.depth);
    id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
      particleFrameCache_.valid = false;
      context_->restartCurrentRenderCommandEncoder(true, true);
      return;
    }
    MTLViewport viewport{0.0,
                         0.0,
                         static_cast<double>(width),
                         static_cast<double>(height),
                         0.0,
                         1.0};
    [encoder setViewport:viewport];
    drawParticles(encoder, frame);
    [encoder endEncoding];

    particleFrameCache_.width = width;
    particleFrameCache_.height = height;
    particleFrameCache_.particlesVersion = particleVersion_;
    particleFrameCache_.model = frame.matrices.model;
    particleFrameCache_.view = frame.matrices.view;
    particleFrameCache_.projection = frame.matrices.projection;
    particleFrameCache_.visualConfig = frame.particleVisual;
    particleFrameCache_.valid = true;
    context_->restartCurrentRenderCommandEncoder(true, true);
  }

  bool drawParticleFrameCache(id<MTLRenderCommandEncoder> encoder)
  {
    if (!particleFrameCache_.valid || !particleFrameCache_.color ||
        !particleFrameCache_.depth || !particleCachePipeline_ ||
        !cacheDepthStencil_) {
      return false;
    }
    [encoder setRenderPipelineState:particleCachePipeline_];
    [encoder setDepthStencilState:cacheDepthStencil_];
    [encoder setFragmentTexture:particleFrameCache_.color atIndex:0];
    [encoder setFragmentTexture:particleFrameCache_.depth atIndex:1];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    return true;
  }

#ifdef VOLUME_RENDERING
  template <typename T>
  id<MTLBuffer> makeSharedBuffer(id<MTLDevice> device,
                                 const std::vector<T>& values)
  {
    if (!device || values.empty()) {
      return nil;
    }
    return [device newBufferWithBytes:values.data()
                               length:values.size() * sizeof(T)
                              options:MTLResourceStorageModeShared];
  }

  void syncVolume(const RenderSceneData& scene)
  {
    if (!scene.volume.valid()) {
      volumeBuffers_ = MetalVolumeBuffers{};
      return;
    }
    if (volumeBuffers_.version == scene.volumeVersion &&
        volumeBuffers_.nodeCount == scene.volume.nodes.size()) {
      return;
    }

    std::vector<MetalFloat4> nodeMin;
    std::vector<MetalFloat4> nodeMax;
    std::vector<MetalInt4> childA;
    std::vector<MetalInt4> childB;
    std::vector<MetalFloat4> cornerLo;
    std::vector<MetalFloat4> cornerHi;
    nodeMin.reserve(scene.volume.nodes.size());
    nodeMax.reserve(scene.volume.nodes.size());
    childA.reserve(scene.volume.nodes.size());
    childB.reserve(scene.volume.nodes.size());
    cornerLo.reserve(scene.volume.nodes.size());
    cornerHi.reserve(scene.volume.nodes.size());

    for (const AdaptiveVolumeTreeNode& node : scene.volume.nodes) {
      MetalFloat4 mn;
      mn.v[0] = node.boundsMin.x;
      mn.v[1] = node.boundsMin.y;
      mn.v[2] = node.boundsMin.z;
      mn.v[3] = node.sigmaAvg;
      nodeMin.push_back(mn);

      MetalFloat4 mx;
      mx.v[0] = node.boundsMax.x;
      mx.v[1] = node.boundsMax.y;
      mx.v[2] = node.boundsMax.z;
      mx.v[3] = node.sigmaMax;
      nodeMax.push_back(mx);

      MetalInt4 ca;
      MetalInt4 cb;
      for (int i = 0; i < 4; ++i) {
        ca.v[i] = node.child[i];
        cb.v[i] = node.child[i + 4];
      }
      childA.push_back(ca);
      childB.push_back(cb);

      MetalFloat4 clo;
      MetalFloat4 chi;
      for (int i = 0; i < 4; ++i) {
        clo.v[i] = node.cornerSigma[i];
        chi.v[i] = node.cornerSigma[i + 4];
      }
      cornerLo.push_back(clo);
      cornerHi.push_back(chi);
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    MetalVolumeBuffers next;
    next.nodeMin = makeSharedBuffer(device, nodeMin);
    next.nodeMax = makeSharedBuffer(device, nodeMax);
    next.childA = makeSharedBuffer(device, childA);
    next.childB = makeSharedBuffer(device, childB);
    next.cornerLo = makeSharedBuffer(device, cornerLo);
    next.cornerHi = makeSharedBuffer(device, cornerHi);
    next.nodeCount = nodeMin.size();
    next.root = scene.volume.root;
    next.version = scene.volumeVersion;

    if (!next.nodeMin || !next.nodeMax || !next.childA || !next.childB ||
        !next.cornerLo || !next.cornerHi) {
      std::cerr << "Metal volume buffer upload failed." << std::endl;
      volumeBuffers_ = MetalVolumeBuffers{};
      return;
    }
    volumeBuffers_ = next;
  }

  bool shouldSkipVolumeForInteraction(const RenderFrameState& frame) const
  {
    const RenderRuntimeState& render = frame.runtime;
    return render.scheduling.responsiveInteraction &&
           render.scheduling.interactionActive &&
           render.scheduling.skipVolumeWhileInteracting;
  }

  bool volumeFrameCacheMatches(const VolumeUniformsCpu& uniforms,
                               int width,
                               int height) const
  {
    return volumeFrameCache_.valid &&
           volumeFrameCache_.volumeVersion == volumeBuffers_.version &&
           volumeFrameCache_.width == width &&
           volumeFrameCache_.height == height &&
           volumeFrameCache_.uniformInitialized &&
           std::memcmp(&volumeFrameCache_.uniforms,
                       &uniforms,
                       sizeof(VolumeUniformsCpu)) == 0;
  }

  void updateVolumeFrameCache(const RenderFrameState& frame)
  {
    timing_.volumeCacheUsed = frame.runtime.scheduling.cacheVolumeFrames;
    timing_.volumeCacheUpdated = false;
    timing_.volumeCacheHit = false;
    timing_.volumeCacheScale =
      std::clamp(static_cast<double>(
                   frame.runtime.scheduling.volumeFrameCacheScale),
                 0.25,
                 1.0);
    if (!frame.runtime.scheduling.cacheVolumeFrames ||
        shouldSkipVolumeForInteraction(frame) ||
        !frame.runtime.volume.show || !volumePipeline_ ||
        volumeBuffers_.nodeCount == 0 || volumeBuffers_.root < 0) {
      return;
    }

    const float cacheScale =
      std::clamp(frame.runtime.scheduling.volumeFrameCacheScale, 0.25f, 1.0f);
    const int cacheWidth =
      std::max(1, static_cast<int>(std::ceil(frame.viewport.width * cacheScale)));
    const int cacheHeight =
      std::max(1, static_cast<int>(std::ceil(frame.viewport.height * cacheScale)));

    VolumeUniformsCpu uniforms;
    FillVolumeUniforms(frame, volumeBuffers_.root, cacheScale, uniforms);
    if (volumeFrameCacheMatches(uniforms, cacheWidth, cacheHeight)) {
      timing_.volumeCacheHit = true;
      return;
    }

    if (!ensureTexture(volumeFrameCache_.color,
                       MTLPixelFormatBGRA8Unorm,
                       cacheWidth,
                       cacheHeight,
                       MTLTextureUsageRenderTarget |
                         MTLTextureUsageShaderRead)) {
      volumeFrameCache_.valid = false;
      return;
    }

    id<MTLCommandBuffer> commandBuffer = currentCommandBuffer();
    if (!commandBuffer) {
      volumeFrameCache_.valid = false;
      return;
    }
    context_->endCurrentRenderCommandEncoder();

    MTLRenderPassDescriptor* pass =
      makeOffscreenPass(volumeFrameCache_.color, nil);
    id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
      volumeFrameCache_.valid = false;
      context_->restartCurrentRenderCommandEncoder(true, true);
      return;
    }

    MTLViewport viewport{0.0,
                         0.0,
                         static_cast<double>(cacheWidth),
                         static_cast<double>(cacheHeight),
                         0.0,
                         1.0};
    [encoder setViewport:viewport];
    drawVolumeWithUniforms(encoder, uniforms);
    [encoder endEncoding];

    volumeFrameCache_.width = cacheWidth;
    volumeFrameCache_.height = cacheHeight;
    volumeFrameCache_.volumeVersion = volumeBuffers_.version;
    volumeFrameCache_.uniforms = uniforms;
    volumeFrameCache_.uniformInitialized = true;
    volumeFrameCache_.valid = true;
    timing_.volumeCacheUpdated = true;
    context_->restartCurrentRenderCommandEncoder(true, true);
  }

  bool drawCachedVolumeFrame(id<MTLRenderCommandEncoder> encoder,
                             const RenderFrameState& frame)
  {
    if (shouldSkipVolumeForInteraction(frame) ||
        !frame.runtime.volume.show ||
        !volumeFrameCache_.valid || !volumeFrameCache_.color ||
        !colorCachePipeline_) {
      return false;
    }
    [encoder setRenderPipelineState:colorCachePipeline_];
    [encoder setDepthStencilState:volumeDepthStencil_];
    [encoder setFragmentTexture:volumeFrameCache_.color atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    return true;
  }

  void drawVolumeWithUniforms(id<MTLRenderCommandEncoder> encoder,
                              const VolumeUniformsCpu& uniforms)
  {
    if (!volumePipeline_ || !volumeDepthStencil_ ||
        !volumeBuffers_.nodeMin || !volumeBuffers_.nodeMax ||
        !volumeBuffers_.childA || !volumeBuffers_.childB ||
        !volumeBuffers_.cornerLo || !volumeBuffers_.cornerHi) {
      return;
    }

    [encoder setRenderPipelineState:volumePipeline_];
    [encoder setDepthStencilState:volumeDepthStencil_];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder setFragmentBuffer:volumeBuffers_.nodeMin offset:0 atIndex:1];
    [encoder setFragmentBuffer:volumeBuffers_.nodeMax offset:0 atIndex:2];
    [encoder setFragmentBuffer:volumeBuffers_.childA offset:0 atIndex:3];
    [encoder setFragmentBuffer:volumeBuffers_.childB offset:0 atIndex:4];
    [encoder setFragmentBuffer:volumeBuffers_.cornerLo offset:0 atIndex:5];
    [encoder setFragmentBuffer:volumeBuffers_.cornerHi offset:0 atIndex:6];
    [encoder setFragmentTexture:colormapTexture_ atIndex:0];
    [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
  }

  void drawVolume(id<MTLRenderCommandEncoder> encoder,
                  const RenderFrameState& frame,
                  float focalScale)
  {
    if (!frame.runtime.volume.show || shouldSkipVolumeForInteraction(frame) ||
        !volumePipeline_ || !volumeDepthStencil_ ||
        volumeBuffers_.nodeCount == 0 || volumeBuffers_.root < 0 ||
        !volumeBuffers_.nodeMin || !volumeBuffers_.nodeMax ||
        !volumeBuffers_.childA || !volumeBuffers_.childB ||
        !volumeBuffers_.cornerLo || !volumeBuffers_.cornerHi) {
      return;
    }

    VolumeUniformsCpu uniforms;
    FillVolumeUniforms(frame, volumeBuffers_.root, focalScale, uniforms);
    drawVolumeWithUniforms(encoder, uniforms);
  }
#endif

  void syncParticles(const RenderSceneData& scene)
  {
    syncParticleBuffer(scene.particles,
                       scene.particlesVersion,
                       particleBuffer_,
                       particleVersion_,
                       particleCount_);
  }

  void syncStressParticles(const RenderSceneData& scene)
  {
    syncParticleBuffer(scene.stressParticles,
                       scene.stressParticlesVersion,
                       stressParticleBuffer_,
                       stressParticleVersion_,
                       stressParticleCount_);
  }

  bool ensureGpuParticleLodBuffers(std::size_t maxOutput,
                                   std::size_t maxStressOutput)
  {
    if (!context_ || maxOutput == 0) {
      return false;
    }
    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    if (!device) {
      return false;
    }

    if (!gpuParticleLodBuffer_ || gpuParticleLodCapacity_ < maxOutput) {
      gpuParticleLodBuffer_ =
        [device newBufferWithLength:maxOutput * sizeof(RenderParticle)
                             options:MTLResourceStorageModeShared];
      gpuParticleLodCapacity_ = gpuParticleLodBuffer_ ? maxOutput : 0;
    }

    const std::size_t stressCapacity = std::max<std::size_t>(1, maxStressOutput);
    if (!gpuStressParticleLodBuffer_ ||
        gpuStressParticleLodCapacity_ < stressCapacity) {
      gpuStressParticleLodBuffer_ =
        [device newBufferWithLength:stressCapacity * sizeof(RenderParticle)
                             options:MTLResourceStorageModeShared];
      gpuStressParticleLodCapacity_ =
        gpuStressParticleLodBuffer_ ? stressCapacity : 0;
    }

    const std::size_t nodeCapacity =
      std::max<std::size_t>(1, particleLodTreeBuffers_.nodeCount);
    if (!gpuLeafRangeLodBuffer_ || gpuLeafRangeLodCapacity_ < nodeCapacity) {
      gpuLeafRangeLodBuffer_ =
        [device newBufferWithLength:nodeCapacity * sizeof(std::uint32_t) * 4u
                             options:MTLResourceStorageModeShared];
      gpuLeafRangeLodCapacity_ = gpuLeafRangeLodBuffer_ ? nodeCapacity : 0;
    }
    if (!gpuParticleLodNodeFlagBuffer_ ||
        gpuParticleLodNodeFlagCapacity_ < nodeCapacity) {
      gpuParticleLodNodeFlagBuffer_ =
        [device newBufferWithLength:nodeCapacity * sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
      gpuParticleLodNodeFlagCapacity_ =
        gpuParticleLodNodeFlagBuffer_ ? nodeCapacity : 0;
    }
    if (!gpuParticleLodCounterBuffer_) {
      gpuParticleLodCounterBuffer_ =
        [device newBufferWithLength:sizeof(std::uint32_t) * 10u
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodLevelStatsBuffer_) {
      gpuParticleLodLevelStatsBuffer_ =
        [device newBufferWithLength:
                  sizeof(std::uint32_t) *
                  RenderBackendTimingInfo::kMaxParticleGpuLodLevels *
                  kParticleGpuLodLevelStatFields
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodIndirectBuffer_) {
      gpuParticleLodIndirectBuffer_ =
        [device newBufferWithLength:sizeof(std::uint32_t) * 4u * 2u
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodQueueA_ || gpuParticleLodQueueCapacity_ < nodeCapacity) {
      gpuParticleLodQueueA_ =
        [device newBufferWithLength:nodeCapacity * sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
      gpuParticleLodQueueB_ =
        [device newBufferWithLength:nodeCapacity * sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
      gpuParticleLodQueueCapacity_ =
        (gpuParticleLodQueueA_ && gpuParticleLodQueueB_) ? nodeCapacity : 0;
    }
    if (!gpuParticleLodQueueCountA_) {
      gpuParticleLodQueueCountA_ =
        [device newBufferWithLength:sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodQueueCountB_) {
      gpuParticleLodQueueCountB_ =
        [device newBufferWithLength:sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodDispatchArgsA_) {
      gpuParticleLodDispatchArgsA_ =
        [device newBufferWithLength:sizeof(std::uint32_t) * 3u
                             options:MTLResourceStorageModeShared];
    }
    if (!gpuParticleLodDispatchArgsB_) {
      gpuParticleLodDispatchArgsB_ =
        [device newBufferWithLength:sizeof(std::uint32_t) * 3u
                             options:MTLResourceStorageModeShared];
    }

    return gpuParticleLodBuffer_ &&
           gpuStressParticleLodBuffer_ &&
           gpuLeafRangeLodBuffer_ &&
           gpuParticleLodNodeFlagBuffer_ &&
           gpuParticleLodCounterBuffer_ &&
           gpuParticleLodLevelStatsBuffer_ &&
           gpuParticleLodIndirectBuffer_ &&
           gpuParticleLodQueueA_ &&
           gpuParticleLodQueueB_ &&
           gpuParticleLodQueueCountA_ &&
           gpuParticleLodQueueCountB_ &&
           gpuParticleLodDispatchArgsA_ &&
           gpuParticleLodDispatchArgsB_;
  }

  void updateGpuParticleLodStatsFromCounters()
  {
    if (!gpuParticleLodCounterBuffer_ || !gpuParticleLodLevelStatsBuffer_) {
      timing_.particleGpuLodStatsKnown = false;
      return;
    }
    const auto* counters =
      static_cast<const std::uint32_t*>([gpuParticleLodCounterBuffer_ contents]);
    if (!counters) {
      timing_.particleGpuLodStatsKnown = false;
      return;
    }
    timing_.particleGpuLodProxyCount = counters[0];
    timing_.particleGpuLodLeafRangeCount = counters[1];
    timing_.particleGpuLodVisitedNodes = counters[4];
    timing_.particleGpuLodFrustumCulledNodes = counters[5];
    timing_.particleGpuLodAcceptedProxyNodes = counters[6];
    timing_.particleGpuLodAcceptedLeafRanges = counters[7];
    timing_.particleGpuLodLeafParticleCount = counters[3];
    timing_.particleGpuLodExpandedNodes = counters[8];
    timing_.particleGpuLodAppendedChildren = counters[9];
    timing_.particleGpuLodGeneratedDrawCommands =
      particleLodNormalDrawFallback_
        ? 1u
        : (counters[0] > 0 ? 1u : 0u) +
            static_cast<std::uint32_t>(particleLodRangeIcbCommandCount_);
    timing_.particleGpuLodMaxLeafCount = particleLodTreeBuffers_.maxLeafCount;
    timing_.particleGpuLodMergedLeafRangeCount =
      static_cast<std::uint64_t>(particleLodRangeIcbCommandCount_);
    const auto* levels = static_cast<const std::uint32_t*>(
      [gpuParticleLodLevelStatsBuffer_ contents]);
    timing_.particleGpuLodLevelCount = 0;
    if (levels) {
      for (std::size_t level = 0;
           level < RenderBackendTimingInfo::kMaxParticleGpuLodLevels;
           ++level) {
        const std::size_t base = level * kParticleGpuLodLevelStatFields;
        auto& dst = timing_.particleGpuLodLevels[level];
        dst = RenderBackendTimingInfo::ParticleGpuLodLevelStats{};
        dst.visited = levels[base + 0];
        dst.proxy = levels[base + 1];
        dst.leaf = levels[base + 2];
        dst.expanded = levels[base + 3];
        if (dst.visited > 0) {
          const std::uint32_t minPx = levels[base + 4];
          const std::uint32_t maxPx = levels[base + 5];
          const std::uint32_t sumPx = levels[base + 6];
          dst.minProjectedPx =
            minPx == 0xffffffffu
              ? 0.0f
              : static_cast<float>(minPx) /
                  static_cast<float>(kParticleGpuLodPxScale);
          dst.maxProjectedPx =
            static_cast<float>(maxPx) /
            static_cast<float>(kParticleGpuLodPxScale);
          dst.avgProjectedPx =
            static_cast<float>(sumPx) /
            static_cast<float>(kParticleGpuLodPxScale) /
            static_cast<float>(dst.visited);
          timing_.particleGpuLodLevelCount =
            static_cast<std::uint32_t>(level + 1);
        }
      }
    }
    timing_.particleGpuLodStatsKnown = true;
  }

  void invalidateParticleLodIcb()
  {
    particleLodRangeIcbValid_ = false;
    particleLodRangeIcbCommandCount_ = 0;
    particleLodNormalDrawFallback_ = false;
    particleLodNeedsIcbBuild_ = false;
    particleLodPendingDrawRanges_.clear();
    particleLodPreviousRawDrawRanges_.clear();
    particleLodPendingRangeCount_ = 0;
    particleLodPendingNormalDrawFallback_ = false;
  }

  bool ensureParticleLodRangeIcb(std::size_t commandCount)
  {
    if (!context_ || commandCount == 0) {
      return false;
    }
    if (particleLodRangeIcb_ && particleLodRangeIcbCapacity_ >= commandCount) {
      return true;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    MTLIndirectCommandBufferDescriptor* desc =
      [[MTLIndirectCommandBufferDescriptor alloc] init];
    desc.commandTypes = MTLIndirectCommandTypeDraw;
    desc.inheritPipelineState = NO;
    desc.inheritBuffers = YES;
    desc.maxVertexBufferBindCount = 2;
    desc.maxFragmentBufferBindCount = 2;
    particleLodRangeIcb_ =
      [device newIndirectCommandBufferWithDescriptor:desc
                                     maxCommandCount:commandCount
                                             options:0];
    particleLodRangeIcbCapacity_ = particleLodRangeIcb_ ? commandCount : 0;
    return particleLodRangeIcb_ != nil;
  }

  bool buildParticleLodRangeIcbFromGpuRanges(bool useTemporalUnion)
  {
    if (!gpuLeafRangeLodBuffer_ || !gpuParticleLodCounterBuffer_ ||
        !particleLodOrderedBuffer_ || particleLodOrderedCount_ == 0) {
      invalidateParticleLodIcb();
      return false;
    }

    const auto buildStart = std::chrono::steady_clock::now();
    updateGpuParticleLodStatsFromCounters();
    const auto* counters =
      static_cast<const std::uint32_t*>([gpuParticleLodCounterBuffer_ contents]);
    const auto* rawRanges =
      static_cast<const std::uint32_t*>([gpuLeafRangeLodBuffer_ contents]);
    const auto* nodeFlags =
      gpuParticleLodNodeFlagBuffer_
        ? static_cast<const std::uint32_t*>(
            [gpuParticleLodNodeFlagBuffer_ contents])
        : nullptr;
    const auto* nodeMeta =
      particleLodTreeBuffers_.nodeMeta
        ? static_cast<const std::uint32_t*>(
            [particleLodTreeBuffers_.nodeMeta contents])
        : nullptr;
    if (!counters || !rawRanges) {
      invalidateParticleLodIcb();
      return false;
    }

    const std::size_t leafRangeCount =
      std::min<std::size_t>(counters[1], gpuLeafRangeLodCapacity_);
    const std::uint64_t coveredParticleCount = counters[3];
    timing_.particleGpuLodLeafParticleCount = coveredParticleCount;

    particleLodPendingDrawRanges_.clear();
    particleLodPendingDrawRanges_.reserve(leafRangeCount);
    bool rangesSorted = true;
    std::uint32_t lastStart = 0;
    bool hasLastStart = false;
    if (nodeFlags && nodeMeta &&
        gpuParticleLodNodeFlagCapacity_ >= particleLodTreeBuffers_.nodeCount) {
      for (std::size_t i = 0; i < particleLodTreeBuffers_.nodeCount; ++i) {
        if ((nodeFlags[i] & 2u) == 0u) {
          continue;
        }
        const std::uint32_t start = nodeMeta[i * 4u + 0u];
        const std::uint32_t count = nodeMeta[i * 4u + 1u];
        if (count == 0 || start >= particleLodOrderedCount_) {
          continue;
        }
        if (hasLastStart && start < lastStart) {
          rangesSorted = false;
        }
        lastStart = start;
        hasLastStart = true;
        const std::size_t available = particleLodOrderedCount_ - start;
        particleLodPendingDrawRanges_.push_back(
          {start,
           static_cast<std::uint32_t>(
             std::min<std::size_t>(count, available))});
      }
    } else {
      rangesSorted = false;
      for (std::size_t i = 0; i < leafRangeCount; ++i) {
        const std::uint32_t start = rawRanges[i * 4u + 0u];
        const std::uint32_t count = rawRanges[i * 4u + 1u];
        if (count == 0 || start >= particleLodOrderedCount_) {
          continue;
        }
        const std::size_t available = particleLodOrderedCount_ - start;
        particleLodPendingDrawRanges_.push_back(
          {start,
           static_cast<std::uint32_t>(
             std::min<std::size_t>(count, available))});
      }
    }
    if (rangesSorted) {
      MergeSortedParticleLodRanges(particleLodPendingDrawRanges_);
    } else {
      SortAndMergeParticleLodRanges(particleLodPendingDrawRanges_);
    }
    std::vector<ParticleLodDrawRange> newRawRanges =
      particleLodPendingDrawRanges_;
    if (useTemporalUnion && !particleLodPreviousRawDrawRanges_.empty()) {
      MergeParticleLodRangeUnion(newRawRanges,
                                 particleLodPreviousRawDrawRanges_,
                                 particleLodPendingDrawRanges_);
    }
    particleLodPreviousRawDrawRanges_.swap(newRawRanges);
    const std::uint64_t maxDrawnParticleCount =
      static_cast<std::uint64_t>(
        kMetalParticleLodNormalFallbackCoverage *
        static_cast<double>(particleCount_));
    CoalesceParticleLodRangesToBudget(
      particleLodPendingDrawRanges_,
      kMetalParticleLodTargetRangeDrawCommands,
      maxDrawnParticleCount);

    const bool coveredAlmostAll =
      particleCount_ > 0 &&
      static_cast<double>(
        CountParticleLodRangeVertices(particleLodPendingDrawRanges_)) >=
        kMetalParticleLodNormalFallbackCoverage *
          static_cast<double>(particleCount_);
    const bool tooManyCommands =
      particleLodPendingDrawRanges_.size() > kMetalParticleLodMaxIcbCommands;
    particleLodPendingNormalDrawFallback_ = coveredAlmostAll || tooManyCommands;
    particleLodPendingRangeCount_ = particleLodPendingDrawRanges_.size();

    if (EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_USE_ICB") &&
        !particleLodPendingNormalDrawFallback_ &&
        !particleLodPendingDrawRanges_.empty()) {
      particleLodRangeIcbValid_ = false;
      particleLodRangeIcbCommandCount_ = 0;
      if (particleLodRangeIcb_) {
        particleLodRetiredRangeIcbs_.push_back(particleLodRangeIcb_);
        if (particleLodRetiredRangeIcbs_.size() > 3) {
          particleLodRetiredRangeIcbs_.erase(
            particleLodRetiredRangeIcbs_.begin());
        }
        particleLodRangeIcb_ = nil;
        particleLodRangeIcbCapacity_ = 0;
      }
      if (!ensureParticleLodRangeIcb(particleLodPendingDrawRanges_.size())) {
        invalidateParticleLodIcb();
        return false;
      }
      [particleLodRangeIcb_
        resetWithRange:NSMakeRange(0, particleLodRangeIcbCapacity_)];
      for (std::size_t i = 0; i < particleLodPendingDrawRanges_.size(); ++i) {
        const ParticleLodDrawRange& range = particleLodPendingDrawRanges_[i];
        id<MTLIndirectRenderCommand> command =
          [particleLodRangeIcb_ indirectRenderCommandAtIndex:i];
        [command setRenderPipelineState:particlePipeline_];
        [command drawPrimitives:MTLPrimitiveTypePoint
                    vertexStart:range.start
                    vertexCount:range.count
                  instanceCount:1
                   baseInstance:0];
      }
    }

    particleLodDrawRanges_.swap(particleLodPendingDrawRanges_);
    particleLodNormalDrawFallback_ = particleLodPendingNormalDrawFallback_;
    particleLodRangeIcbCommandCount_ = particleLodPendingRangeCount_;
    particleLodRangeIcbValid_ =
      EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_USE_ICB") &&
      !particleLodNormalDrawFallback_ &&
      particleLodRangeIcb_ &&
      particleLodRangeIcbCommandCount_ > 0;

    const auto buildEnd = std::chrono::steady_clock::now();
    timing_.particleGpuLodIcbGenerationMs =
      std::chrono::duration<double, std::milli>(
        buildEnd - buildStart).count();
    timing_.particleGpuLodIcbGenerationTimeKnown = true;
    timing_.particleGpuLodMergedLeafRangeCount =
      static_cast<std::uint64_t>(particleLodRangeIcbCommandCount_);
    timing_.particleGpuLodGeneratedDrawCommands =
      particleLodNormalDrawFallback_
        ? 1u
        : (timing_.particleGpuLodProxyCount > 0 ? 1u : 0u) +
            static_cast<std::uint64_t>(particleLodRangeIcbCommandCount_);
    particleLodNeedsIcbBuild_ = false;
    return particleLodRangeIcbValid_ || particleLodNormalDrawFallback_;
  }

  bool drawParticleLodIcb(id<MTLRenderCommandEncoder> encoder,
                          const RenderFrameState& frame)
  {
    if (!encoder) {
      return false;
    }
    if (particleLodNormalDrawFallback_) {
      const auto drawStart = std::chrono::steady_clock::now();
      drawParticles(encoder, frame);
      const auto drawEnd = std::chrono::steady_clock::now();
      timing_.particleGpuLodNormalDrawMs =
        std::chrono::duration<double, std::milli>(
          drawEnd - drawStart).count();
      timing_.particleGpuLodNormalDrawTimeKnown = true;
      return true;
    }

    bool drew = false;
    if (gpuParticleLodIndirectBuffer_) {
      drawParticleBufferIndirect(encoder,
                                 gpuParticleLodBuffer_,
                                 gpuParticleLodIndirectBuffer_,
                                 0,
                                 frame);
      drew = timing_.particleGpuLodProxyCount > 0;
    }

    const bool useIcb =
      EnvFlagEnabled("PARTICLE_VIS_METAL_LOD_USE_ICB") &&
      particleLodRangeIcbValid_ &&
      particleLodRangeIcb_ &&
      particleLodRangeIcbCommandCount_ > 0;
    if (useIcb) {
      ParticleUniformsCpu uniforms;
      FillParticleUniforms(frame, uniforms);
      const auto drawStart = std::chrono::steady_clock::now();
      [encoder setRenderPipelineState:particlePipeline_];
      [encoder setDepthStencilState:depthStencil_];
      [encoder setVertexBuffer:particleLodOrderedBuffer_ offset:0 atIndex:0];
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder setFragmentTexture:colormapTexture_ atIndex:0];
      [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
      [encoder executeCommandsInBuffer:particleLodRangeIcb_
                             withRange:NSMakeRange(
                               0, particleLodRangeIcbCommandCount_)];
      const auto drawEnd = std::chrono::steady_clock::now();
      timing_.particleGpuLodIcbDrawMs =
        std::chrono::duration<double, std::milli>(
          drawEnd - drawStart).count();
      timing_.particleGpuLodIcbDrawTimeKnown = true;
      drew = true;
    } else if (!particleLodDrawRanges_.empty()) {
      ParticleUniformsCpu uniforms;
      FillParticleUniforms(frame, uniforms);
      const auto drawStart = std::chrono::steady_clock::now();
      [encoder setRenderPipelineState:particlePipeline_];
      [encoder setDepthStencilState:depthStencil_];
      [encoder setVertexBuffer:particleLodOrderedBuffer_ offset:0 atIndex:0];
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder setFragmentTexture:colormapTexture_ atIndex:0];
      [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
      for (const ParticleLodDrawRange& range : particleLodDrawRanges_) {
        if (range.count == 0) {
          continue;
        }
        [encoder drawPrimitives:MTLPrimitiveTypePoint
                    vertexStart:range.start
                    vertexCount:range.count];
      }
      const auto drawEnd = std::chrono::steady_clock::now();
      timing_.particleGpuLodIcbDrawMs =
        std::chrono::duration<double, std::milli>(
          drawEnd - drawStart).count();
      timing_.particleGpuLodIcbDrawTimeKnown = true;
      drew = true;
    }
    return drew && (particleLodNormalDrawFallback_ ||
                    particleLodRangeIcbValid_ ||
                    !particleLodDrawRanges_.empty() ||
                    timing_.particleGpuLodProxyCount > 0);
  }

  bool gpuParticleLodCacheMatches(const RenderFrameState& frame,
                                  const RenderSceneData& scene) const
  {
    return gpuParticleLodCache_.valid &&
           gpuParticleLodCache_.version == scene.particleLodVersion &&
           gpuParticleLodCache_.cameraPos == frame.camera.cameraPos &&
           gpuParticleLodCache_.cameraTarget == frame.camera.cameraTarget &&
           gpuParticleLodCache_.cameraDistance == frame.camera.distance &&
           gpuParticleLodCache_.focalPx == frame.matrices.focalPx &&
           gpuParticleLodCache_.theta == scene.particleLodSettings.theta &&
           gpuParticleLodCache_.screenPixelThreshold ==
             scene.particleLodSettings.screenPixelThreshold;
  }

  bool gpuParticleLodCanReuseDuringInteraction(const RenderFrameState& frame,
                                               const RenderSceneData& scene) const
  {
    if (!gpuParticleLodCache_.valid ||
        gpuParticleLodCache_.version != scene.particleLodVersion ||
        !frame.runtime.scheduling.interactionActive) {
      return false;
    }
    const float updateHz = scene.particleLodSettings.proxyUpdateRateHz;
    if (updateHz <= 0.0f) {
      return true;
    }
    const double minInterval = 1.0 / static_cast<double>(updateHz);
    return frame.runtime.scheduling.currentTimeSeconds -
             gpuParticleLodCache_.lastUpdateTime <
           minInterval;
  }

  void markGpuParticleLodCacheValid(const RenderFrameState& frame,
                                    const RenderSceneData& scene)
  {
    gpuParticleLodCache_.version = scene.particleLodVersion;
    gpuParticleLodCache_.cameraPos = frame.camera.cameraPos;
    gpuParticleLodCache_.cameraTarget = frame.camera.cameraTarget;
    gpuParticleLodCache_.cameraDistance = frame.camera.distance;
    gpuParticleLodCache_.focalPx = frame.matrices.focalPx;
    gpuParticleLodCache_.theta = scene.particleLodSettings.theta;
    gpuParticleLodCache_.screenPixelThreshold =
      scene.particleLodSettings.screenPixelThreshold;
    gpuParticleLodCache_.lastUpdateTime =
      frame.runtime.scheduling.currentTimeSeconds;
    gpuParticleLodCache_.valid = true;
  }

  bool encodeGpuParticleLodProxy(const RenderFrameState& frame,
                                 const RenderSceneData& scene,
                                 bool restartLoadExisting = true)
  {
    timing_.particleGpuLodActive = true;
    timing_.particleGpuLodUpdated = false;
    timing_.particleGpuLodCacheHit = false;
    if (particleLodNeedsIcbBuild_ && particleLodRangeDataReady_.load()) {
      buildParticleLodRangeIcbFromGpuRanges(
        frame.runtime.scheduling.interactionActive);
    }
    if (!experimentalGpuParticleLod_ ||
        !particleLodFrontierPipeline_ ||
        !particleLodResetQueuePipeline_ ||
        !particleLodDispatchArgsPipeline_ ||
        !particleLodFinalizePipeline_ ||
        !particleBuffer_ ||
        !particleLodOrderedBuffer_ ||
        !particleLodTreeBuffers_.nodeCenterRadius ||
        !scene.particleLodGpu.valid ||
        particleLodTreeBuffers_.nodeCount == 0) {
      return false;
    }

    if (gpuParticleLodCacheMatches(frame, scene)) {
      timing_.particleGpuLodCacheHit = true;
      return true;
    }
    if (gpuParticleLodCanReuseDuringInteraction(frame, scene)) {
      timing_.particleGpuLodCacheHit = true;
      return true;
    }
    const auto lodStart = std::chrono::steady_clock::now();
    const std::size_t maxOutput = particleCount_;
    const std::size_t maxStressOutput =
      std::max<std::size_t>(1, stressParticleCount_);
    if (!ensureGpuParticleLodBuffers(maxOutput, maxStressOutput)) {
      return false;
    }
    updateGpuParticleLodStatsFromCounters();

    auto* counters =
      static_cast<std::uint32_t*>([gpuParticleLodCounterBuffer_ contents]);
    std::memset(counters, 0, sizeof(std::uint32_t) * 10u);
    auto* levelStats =
      static_cast<std::uint32_t*>([gpuParticleLodLevelStatsBuffer_ contents]);
    if (levelStats) {
      const std::size_t levelValueCount =
        RenderBackendTimingInfo::kMaxParticleGpuLodLevels *
        kParticleGpuLodLevelStatFields;
      std::memset(levelStats, 0, sizeof(std::uint32_t) * levelValueCount);
      for (std::size_t level = 0;
           level < RenderBackendTimingInfo::kMaxParticleGpuLodLevels;
           ++level) {
        levelStats[level * kParticleGpuLodLevelStatFields + 4] = 0xffffffffu;
      }
    }
    std::memset([gpuParticleLodIndirectBuffer_ contents],
                0,
                sizeof(std::uint32_t) * 4u * 2u);
    std::memset([gpuParticleLodNodeFlagBuffer_ contents],
                0,
                gpuParticleLodNodeFlagCapacity_ * sizeof(std::uint32_t));
    static_cast<std::uint32_t*>([gpuParticleLodQueueA_ contents])[0] = 0;
    static_cast<std::uint32_t*>([gpuParticleLodQueueCountA_ contents])[0] = 1;
    static_cast<std::uint32_t*>([gpuParticleLodQueueCountB_ contents])[0] = 0;
    auto* dispatchA =
      static_cast<std::uint32_t*>([gpuParticleLodDispatchArgsA_ contents]);
    auto* dispatchB =
      static_cast<std::uint32_t*>([gpuParticleLodDispatchArgsB_ contents]);
    dispatchA[0] = 1;
    dispatchA[1] = 1;
    dispatchA[2] = 1;
    dispatchB[0] = 1;
    dispatchB[1] = 1;
    dispatchB[2] = 1;

    ParticleLodComputeUniformsCpu uniforms;
    uniforms.cameraPosFocalPx[0] = frame.camera.cameraPos.x;
    uniforms.cameraPosFocalPx[1] = frame.camera.cameraPos.y;
    uniforms.cameraPosFocalPx[2] = frame.camera.cameraPos.z;
    uniforms.cameraPosFocalPx[3] = frame.matrices.focalPx;
    uniforms.focusPosProtectRadius[0] = frame.camera.cameraTarget.x;
    uniforms.focusPosProtectRadius[1] = frame.camera.cameraTarget.y;
    uniforms.focusPosProtectRadius[2] = frame.camera.cameraTarget.z;
    uniforms.focusPosProtectRadius[3] = 0.0f;
    uniforms.lodParams[0] =
      std::max(0.05f, scene.particleLodSettings.screenPixelThreshold);
    uniforms.lodParams[1] = 2.0f;
    uniforms.lodParams[2] = 0.75f;
    uniforms.lodParams[3] = 1.75f;
    FillFrustumPlanes(frame.matrices.projection *
                        frame.matrices.view *
                        frame.matrices.model,
                      uniforms.frustumPlanes);
    uniforms.nodeCount =
      static_cast<std::uint32_t>(particleLodTreeBuffers_.nodeCount);
    uniforms.maxOutput = static_cast<std::uint32_t>(maxOutput);
    uniforms.maxLeafOutput = static_cast<std::uint32_t>(maxOutput);
    uniforms.queueCapacity =
      static_cast<std::uint32_t>(gpuParticleLodQueueCapacity_);
    const NSUInteger lodThreadsPerGroup =
      std::max<NSUInteger>(1, particleLodFrontierPipeline_.threadExecutionWidth);
    uniforms.threadsPerGroup =
      static_cast<std::uint32_t>(lodThreadsPerGroup);
    uniforms.maxLevelStats =
      static_cast<std::uint32_t>(
        RenderBackendTimingInfo::kMaxParticleGpuLodLevels);
    uniforms.maxLeafDrawCount =
      std::max<std::uint32_t>(1u, particleLodTreeBuffers_.maxLeafCount);

    context_->endCurrentRenderCommandEncoder();
    id<MTLCommandBuffer> commandBuffer = currentCommandBuffer();
    if (!commandBuffer) {
      return false;
    }

    const std::uint32_t maxPasses =
      std::min<std::uint32_t>(
        static_cast<std::uint32_t>(particleLodTreeBuffers_.nodeCount),
        std::max<std::uint32_t>(
          1u,
          static_cast<std::uint32_t>(scene.particleLodSettings.maxDepth + 2)));

    id<MTLBuffer> activeQueue = gpuParticleLodQueueA_;
    id<MTLBuffer> nextQueue = gpuParticleLodQueueB_;
    id<MTLBuffer> activeCount = gpuParticleLodQueueCountA_;
    id<MTLBuffer> nextCount = gpuParticleLodQueueCountB_;
    id<MTLBuffer> activeDispatchArgs = gpuParticleLodDispatchArgsA_;
    id<MTLBuffer> nextDispatchArgs = gpuParticleLodDispatchArgsB_;

    for (std::uint32_t pass = 0; pass < maxPasses; ++pass) {
      uniforms.levelIndex = std::min<std::uint32_t>(
        pass,
        static_cast<std::uint32_t>(
          RenderBackendTimingInfo::kMaxParticleGpuLodLevels - 1));
      id<MTLComputeCommandEncoder> frontierEncoder =
        [commandBuffer computeCommandEncoder];
      if (!frontierEncoder) {
        return false;
      }
      [frontierEncoder setComputePipelineState:particleLodFrontierPipeline_];
      [frontierEncoder setBuffer:particleBuffer_ offset:0 atIndex:0];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.nodeCenterRadius
                          offset:0
                         atIndex:1];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.representativePosHsml
                          offset:0
                         atIndex:2];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.representativeValue
                          offset:0
                         atIndex:3];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.nodeMeta
                          offset:0
                         atIndex:4];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.childA
                          offset:0
                         atIndex:5];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.childB
                          offset:0
                         atIndex:6];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.representativeMeta
                          offset:0
                         atIndex:7];
      [frontierEncoder setBuffer:particleLodTreeBuffers_.indices
                          offset:0
                         atIndex:8];
      [frontierEncoder setBuffer:gpuParticleLodBuffer_ offset:0 atIndex:9];
      [frontierEncoder setBuffer:gpuParticleLodCounterBuffer_
                          offset:0
                         atIndex:10];
      [frontierEncoder setBytes:&uniforms length:sizeof(uniforms) atIndex:11];
      [frontierEncoder setBuffer:activeQueue offset:0 atIndex:12];
      [frontierEncoder setBuffer:nextQueue offset:0 atIndex:13];
      [frontierEncoder setBuffer:activeCount offset:0 atIndex:14];
      [frontierEncoder setBuffer:nextCount offset:0 atIndex:15];
      [frontierEncoder setBuffer:gpuLeafRangeLodBuffer_ offset:0 atIndex:16];
      [frontierEncoder setBuffer:gpuParticleLodLevelStatsBuffer_
                          offset:0
                         atIndex:17];
      [frontierEncoder setBuffer:gpuParticleLodNodeFlagBuffer_
                          offset:0
                         atIndex:18];
      [frontierEncoder dispatchThreadgroupsWithIndirectBuffer:activeDispatchArgs
                                         indirectBufferOffset:0
                                        threadsPerThreadgroup:
                                          MTLSizeMake(lodThreadsPerGroup, 1, 1)];
      [frontierEncoder endEncoding];

      id<MTLComputeCommandEncoder> dispatchEncoder =
        [commandBuffer computeCommandEncoder];
      if (!dispatchEncoder) {
        return false;
      }
      [dispatchEncoder setComputePipelineState:particleLodDispatchArgsPipeline_];
      [dispatchEncoder setBuffer:nextCount offset:0 atIndex:0];
      [dispatchEncoder setBuffer:nextDispatchArgs offset:0 atIndex:1];
      [dispatchEncoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];
      [dispatchEncoder setBuffer:activeCount offset:0 atIndex:3];
      [dispatchEncoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                      threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
      [dispatchEncoder endEncoding];

      std::swap(activeQueue, nextQueue);
      std::swap(activeCount, nextCount);
      std::swap(activeDispatchArgs, nextDispatchArgs);
    }

    id<MTLComputeCommandEncoder> finalizeEncoder =
      [commandBuffer computeCommandEncoder];
    if (!finalizeEncoder) {
      return false;
    }
    [finalizeEncoder setComputePipelineState:particleLodFinalizePipeline_];
    [finalizeEncoder setBuffer:gpuParticleLodCounterBuffer_
                        offset:0
                       atIndex:0];
    [finalizeEncoder setBuffer:gpuParticleLodIndirectBuffer_
                        offset:0
                       atIndex:1];
    [finalizeEncoder setBytes:&uniforms
                       length:sizeof(uniforms)
                      atIndex:2];
    [finalizeEncoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
    [finalizeEncoder endEncoding];

    if (!context_->restartCurrentRenderCommandEncoder(restartLoadExisting,
                                                      restartLoadExisting)) {
      return false;
    }
    markGpuParticleLodCacheValid(frame, scene);
    particleLodNeedsIcbBuild_ = true;
    particleLodRangeDataReady_.store(false);
    MetalRenderBackend* self = this;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      self->particleLodRangeDataReady_.store(true);
    }];
    const auto lodEnd = std::chrono::steady_clock::now();
    timing_.particleGpuLodWallMs =
      std::chrono::duration<double, std::milli>(lodEnd - lodStart).count();
    timing_.particleGpuLodWallTimeKnown = true;
    timing_.particleGpuLodUpdated = true;
    timing_.particleGpuLodRefreshHz =
      timing_.particleGpuLodWallMs > 0.0
        ? 1000.0 / timing_.particleGpuLodWallMs
        : 0.0;
    return true;
  }

  template <typename T>
  id<MTLBuffer> makeSharedVectorBuffer(const std::vector<T>& values)
  {
    if (values.empty() || !context_) {
      return nil;
    }
    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    return [device newBufferWithBytes:values.data()
                               length:values.size() * sizeof(T)
                              options:MTLResourceStorageModeShared];
  }

  void syncParticleBuffer(const std::vector<RenderParticle>& particles,
                          RenderSceneVersion version,
                          id<MTLBuffer>& buffer,
                          RenderSceneVersion& uploadedVersion,
                          std::size_t& count)
  {
    if (version == uploadedVersion && buffer) {
      count = particles.size();
      return;
    }
    uploadedVersion = version;
    count = particles.size();
    if (count == 0) {
      buffer = nil;
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    buffer = [device newBufferWithBytes:particles.data()
                                 length:count * sizeof(RenderParticle)
                                options:MTLResourceStorageModeShared];
  }

  void syncParticleLodTree(const RenderSceneData& scene)
  {
    if (particleLodTreeBuffers_.version == scene.particleLodVersion) {
      return;
    }
    invalidateParticleLodIcb();

    const ParticleLodGpuTree& tree = scene.particleLodGpu;
    if (!tree.valid || tree.nodeCenterRadius.empty()) {
      particleLodTreeBuffers_ = MetalParticleLodTreeBuffers{};
      return;
    }

    MetalParticleLodTreeBuffers next;
    next.nodeCenterRadius = makeSharedVectorBuffer(tree.nodeCenterRadius);
    next.representativePosHsml =
      makeSharedVectorBuffer(tree.representativePosHsml);
    next.representativeValue = makeSharedVectorBuffer(tree.representativeValue);
    next.nodeMeta = makeSharedVectorBuffer(tree.nodeMeta);
    next.childA = makeSharedVectorBuffer(tree.childA);
    next.childB = makeSharedVectorBuffer(tree.childB);
    next.representativeMeta = makeSharedVectorBuffer(tree.representativeMeta);
    next.indices = makeSharedVectorBuffer(tree.indices);
    next.nodeCount = tree.nodeCenterRadius.size();
    next.indexCount = tree.indices.size();
    next.maxLeafCount = std::max<std::uint32_t>(1u, tree.maxLeafCount);
    next.version = scene.particleLodVersion;

    if (!next.nodeCenterRadius ||
        !next.representativePosHsml ||
        !next.representativeValue ||
        !next.nodeMeta ||
        !next.childA ||
        !next.childB ||
        !next.representativeMeta ||
        !next.indices) {
      particleLodTreeBuffers_ = MetalParticleLodTreeBuffers{};
      return;
    }

    particleLodTreeBuffers_ = next;
  }

  MetalContext* context_ = nullptr;
  id<MTLRenderPipelineState> particlePipeline_ = nil;
  id<MTLRenderPipelineState> particleIndexedPipeline_ = nil;
  id<MTLRenderPipelineState> particleRangePipeline_ = nil;
  id<MTLRenderPipelineState> linePipeline_ = nil;
  id<MTLRenderPipelineState> solidPipeline_ = nil;
  id<MTLRenderPipelineState> colorCachePipeline_ = nil;
  id<MTLRenderPipelineState> particleCachePipeline_ = nil;
  id<MTLComputePipelineState> particleLodFrontierPipeline_ = nil;
  id<MTLComputePipelineState> particleLodSinglePassPipeline_ = nil;
  id<MTLComputePipelineState> particleLodResetQueuePipeline_ = nil;
  id<MTLComputePipelineState> particleLodDispatchArgsPipeline_ = nil;
  id<MTLComputePipelineState> particleLodFinalizePipeline_ = nil;
#ifdef VOLUME_RENDERING
  id<MTLRenderPipelineState> volumePipeline_ = nil;
#endif
  id<MTLDepthStencilState> depthStencil_ = nil;
  id<MTLDepthStencilState> lineDepthStencil_ = nil;
  id<MTLDepthStencilState> cacheDepthStencil_ = nil;
#ifdef VOLUME_RENDERING
  id<MTLDepthStencilState> volumeDepthStencil_ = nil;
#endif
  id<MTLTexture> colormapTexture_ = nil;
  id<MTLTexture> previewTexture_ = nil;
  id<MTLSamplerState> colormapSampler_ = nil;
  id<MTLBuffer> particleBuffer_ = nil;
  id<MTLBuffer> stressParticleBuffer_ = nil;
  id<MTLBuffer> particleLodOrderedBuffer_ = nil;
  id<MTLBuffer> particleLodBuffer_ = nil;
  id<MTLBuffer> stressParticleLodBuffer_ = nil;
  id<MTLIndirectCommandBuffer> particleLodRangeIcb_ = nil;
  std::vector<id<MTLIndirectCommandBuffer>> particleLodRetiredRangeIcbs_;
  id<MTLBuffer> gpuParticleLodBuffer_ = nil;
  id<MTLBuffer> gpuStressParticleLodBuffer_ = nil;
  id<MTLBuffer> gpuLeafRangeLodBuffer_ = nil;
  id<MTLBuffer> gpuParticleLodNodeFlagBuffer_ = nil;
  id<MTLBuffer> gpuParticleLodLevelStatsBuffer_ = nil;
  id<MTLBuffer> gpuParticleLodCounterBuffer_ = nil;
  id<MTLBuffer> gpuParticleLodIndirectBuffer_ = nil;
  id<MTLBuffer> gpuParticleLodQueueA_ = nil;
  id<MTLBuffer> gpuParticleLodQueueB_ = nil;
  id<MTLBuffer> gpuParticleLodQueueCountA_ = nil;
  id<MTLBuffer> gpuParticleLodQueueCountB_ = nil;
  id<MTLBuffer> gpuParticleLodDispatchArgsA_ = nil;
  id<MTLBuffer> gpuParticleLodDispatchArgsB_ = nil;
  MetalParticleLodTreeBuffers particleLodTreeBuffers_;
  std::vector<ParticleLodDrawRange> particleLodDrawRanges_;
  std::vector<ParticleLodDrawRange> particleLodPendingDrawRanges_;
  std::vector<ParticleLodDrawRange> particleLodPreviousRawDrawRanges_;
  MetalLineVertexSet lineVertices_;
  MetalLineVertexSet cuboidVertices_;
  MetalLineVertexSet polyhedronVertices_;
  MetalLineVertexSet velocityVertices_;
  MetalMesh cubeMesh_;
  MetalMesh diskMesh_;
  MetalMesh sphereMesh_;
  MetalInstanceSet cubeInstances_;
  MetalInstanceSet diskInstances_;
  MetalInstanceSet ellipsoidInstances_;
  MetalParticleFrameCache particleFrameCache_;
#ifdef VOLUME_RENDERING
  MetalVolumeBuffers volumeBuffers_;
  MetalVolumeFrameCache volumeFrameCache_;
#endif
#ifdef ISO_CONTOUR
  MetalMesh isoContourMesh_;
  MetalInstanceSet isoContourInstances_;
  RenderSceneVersion isoContourVersion_ = 0;
#endif
  std::size_t particleCount_ = 0;
  std::size_t stressParticleCount_ = 0;
  std::size_t particleLodOrderedCount_ = 0;
  std::size_t particleLodCount_ = 0;
  std::size_t stressParticleLodCount_ = 0;
  std::size_t particleLodRangeIcbCapacity_ = 0;
  std::size_t particleLodRangeIcbCommandCount_ = 0;
  std::size_t particleLodPendingRangeCount_ = 0;
  std::size_t gpuParticleLodCount_ = 0;
  std::size_t gpuStressParticleLodCount_ = 0;
  std::size_t gpuParticleLodCapacity_ = 0;
  std::size_t gpuStressParticleLodCapacity_ = 0;
  std::size_t gpuLeafRangeLodCapacity_ = 0;
  std::size_t gpuParticleLodNodeFlagCapacity_ = 0;
  std::size_t gpuParticleLodQueueCapacity_ = 0;
  MetalGpuParticleLodCache gpuParticleLodCache_;
  RenderSceneVersion particleVersion_ = 0;
  RenderSceneVersion stressParticleVersion_ = 0;
  RenderSceneVersion particleLodOrderedVersion_ = 0;
  RenderSceneVersion particleLodVersion_ = 0;
  RenderSceneVersion stressParticleLodVersion_ = 0;
  RenderSceneVersion gpuParticleLodMismatchVersion_ = 0;
  RenderSceneVersion velocityVersion_ = 0;
  std::uint64_t previewVersion_ = 0;
  float velocityArrowScale_ = 0.0f;
  bool velocityUseLogScale_ = false;
  bool useParticleLod_ = false;
  bool experimentalGpuParticleLod_ = false;
  bool particleLodRangeIcbValid_ = false;
  bool particleLodNormalDrawFallback_ = false;
  bool particleLodPendingNormalDrawFallback_ = false;
  bool particleLodNeedsIcbBuild_ = false;
  std::atomic_bool particleLodRangeDataReady_{false};
  bool initialized_ = false;
  RenderBackendTimingInfo timing_;
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateMetalRenderBackend(MetalContext& context)
{
  return std::make_unique<MetalRenderBackend>(context);
}
