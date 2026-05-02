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
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace {

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
  float4 misc; // pointScale, globalAlpha, colormapCount, pad.
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
                        VolumeUniformsCpu& uniforms)
{
  const RenderRuntimeState& render = frame.runtime;
  CopyMat4(frame.matrices.invProj, uniforms.invProjection);
  CopyMat4(frame.matrices.invView, uniforms.invView);
  uniforms.cameraForwardFocal[0] = frame.matrices.camForward.x;
  uniforms.cameraForwardFocal[1] = frame.matrices.camForward.y;
  uniforms.cameraForwardFocal[2] = frame.matrices.camForward.z;
  uniforms.cameraForwardFocal[3] = frame.matrices.focalPx;
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
    id<MTLFunction> fragment =
      [library newFunctionWithName:@"particleFragment"];
    id<MTLFunction> lineVertex = [library newFunctionWithName:@"lineVertex"];
    id<MTLFunction> lineFragment =
      [library newFunctionWithName:@"lineFragment"];
    id<MTLFunction> solidVertex = [library newFunctionWithName:@"solidVertex"];
    id<MTLFunction> solidFragment =
      [library newFunctionWithName:@"solidFragment"];
#ifdef VOLUME_RENDERING
    id<MTLFunction> volumeVertex =
      [library newFunctionWithName:@"volumeVertex"];
    id<MTLFunction> volumeFragment =
      [library newFunctionWithName:@"volumeFragment"];
#endif
    if (!vertex || !fragment) {
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
    particlePipeline_ = nil;
    linePipeline_ = nil;
    solidPipeline_ = nil;
#ifdef VOLUME_RENDERING
    volumePipeline_ = nil;
#endif
    depthStencil_ = nil;
    lineDepthStencil_ = nil;
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
#ifdef VOLUME_RENDERING
    volumeBuffers_ = MetalVolumeBuffers{};
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
    particleVersion_ = 0;
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
#ifdef VOLUME_RENDERING
    syncVolume(scene);
#endif
    syncLineVertices(scene, frame.runtime.velocity);
    syncSolidInstances(scene);
#ifdef ISO_CONTOUR
    syncIsoContour(scene);
#endif

    id<MTLRenderCommandEncoder> encoder =
      (__bridge id<MTLRenderCommandEncoder>)
        context_->currentRenderCommandEncoder();
    if (!encoder) {
      return;
    }

    ParticleUniformsCpu uniforms;
    FillParticleUniforms(frame, uniforms);

    MTLViewport viewport{
      static_cast<double>(frame.viewport.x),
      static_cast<double>(frame.viewport.y),
      static_cast<double>(frame.viewport.width),
      static_cast<double>(frame.viewport.height),
      0.0,
      1.0
    };
    [encoder setViewport:viewport];
#ifdef VOLUME_RENDERING
    drawVolume(encoder, frame);
#endif
    if (particlePipeline_ && particleBuffer_ && particleCount_ > 0) {
      [encoder setRenderPipelineState:particlePipeline_];
      [encoder setDepthStencilState:depthStencil_];
      [encoder setVertexBuffer:particleBuffer_ offset:0 atIndex:0];
      [encoder setVertexBytes:&uniforms
                       length:sizeof(uniforms)
                      atIndex:1];
      [encoder setFragmentBytes:&uniforms
                         length:sizeof(uniforms)
                        atIndex:1];
      [encoder setFragmentTexture:colormapTexture_ atIndex:0];
      [encoder setFragmentSamplerState:colormapSampler_ atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypePoint
                  vertexStart:0
                  vertexCount:particleCount_];
    }

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
    caps.velocityField = initialized_;
    caps.instancedObjects = initialized_;
    caps.lines = initialized_;
    caps.polyhedra = initialized_;
    caps.colorbar = initialized_;
    caps.gizmos = initialized_;
    caps.projectionPreview = initialized_;
#ifdef VOLUME_RENDERING
    caps.volumeRendering = initialized_;
#endif
#ifdef ISO_CONTOUR
    caps.isoContour = initialized_;
#endif
    return caps;
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

  void drawVolume(id<MTLRenderCommandEncoder> encoder,
                  const RenderFrameState& frame)
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
    FillVolumeUniforms(frame, volumeBuffers_.root, uniforms);

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
#endif

  void syncParticles(const RenderSceneData& scene)
  {
    if (scene.particlesVersion == particleVersion_ && particleBuffer_) {
      return;
    }
    particleVersion_ = scene.particlesVersion;
    particleCount_ = scene.particles.size();
    if (particleCount_ == 0) {
      particleBuffer_ = nil;
      return;
    }

    id<MTLDevice> device =
      (__bridge id<MTLDevice>)context_->device();
    particleBuffer_ =
      [device newBufferWithBytes:scene.particles.data()
                          length:particleCount_ * sizeof(RenderParticle)
                         options:MTLResourceStorageModeShared];
  }

  MetalContext* context_ = nullptr;
  id<MTLRenderPipelineState> particlePipeline_ = nil;
  id<MTLRenderPipelineState> linePipeline_ = nil;
  id<MTLRenderPipelineState> solidPipeline_ = nil;
#ifdef VOLUME_RENDERING
  id<MTLRenderPipelineState> volumePipeline_ = nil;
#endif
  id<MTLDepthStencilState> depthStencil_ = nil;
  id<MTLDepthStencilState> lineDepthStencil_ = nil;
#ifdef VOLUME_RENDERING
  id<MTLDepthStencilState> volumeDepthStencil_ = nil;
#endif
  id<MTLTexture> colormapTexture_ = nil;
  id<MTLTexture> previewTexture_ = nil;
  id<MTLSamplerState> colormapSampler_ = nil;
  id<MTLBuffer> particleBuffer_ = nil;
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
#ifdef VOLUME_RENDERING
  MetalVolumeBuffers volumeBuffers_;
#endif
#ifdef ISO_CONTOUR
  MetalMesh isoContourMesh_;
  MetalInstanceSet isoContourInstances_;
  RenderSceneVersion isoContourVersion_ = 0;
#endif
  std::size_t particleCount_ = 0;
  RenderSceneVersion particleVersion_ = 0;
  RenderSceneVersion velocityVersion_ = 0;
  std::uint64_t previewVersion_ = 0;
  float velocityArrowScale_ = 0.0f;
  bool velocityUseLogScale_ = false;
  bool initialized_ = false;
  ProjectionPreviewUIState preview_;
};

std::unique_ptr<RenderBackend> CreateMetalRenderBackend(MetalContext& context)
{
  return std::make_unique<MetalRenderBackend>(context);
}
