#version 450

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 7) uniform sampler2D colormapAtlas;

layout(set = 0, binding = 0) uniform VolumeParams {
  mat4 invProj;
  mat4 invView;
  vec4 cameraForwardFocal;   // xyz camera forward, w focal pixels
  vec4 rayParams;            // x pixelThreshold, y tauMax, z sample step length, w skipEpsilon
  vec4 baseColorAndMode;     // rgb base color, w color mode
  vec4 tfRangeScale;         // x valueMin, y valueMax, z sigmaScale, w maxSigma
  ivec4 tfControl;           // x logScale, y component count, z root, w debug mode
  ivec4 colorControl;        // x colormap row, y colormap row count
  vec4 opticalParams;        // x model, y emission scale, z absorption scale, w max samples
  ivec4 tfType[4];
  ivec4 tfLogDomain[4];
  vec4 tfCenter[4];
  vec4 tfWidth[4];
  vec4 tfAmp[4];
} params;

layout(set = 0, binding = 1, std430) readonly buffer VolumeNodeMin {
  vec4 nodeMin[];
} volumeNodeMin;
layout(set = 0, binding = 2, std430) readonly buffer VolumeNodeMax {
  vec4 nodeMax[];
} volumeNodeMax;
layout(set = 0, binding = 3, std430) readonly buffer VolumeChildA {
  ivec4 childA[];
} volumeChildA;
layout(set = 0, binding = 4, std430) readonly buffer VolumeChildB {
  ivec4 childB[];
} volumeChildB;
layout(set = 0, binding = 5, std430) readonly buffer VolumeCornerLo {
  vec4 cornerLo[];
} volumeCornerLo;
layout(set = 0, binding = 6, std430) readonly buffer VolumeCornerHi {
  vec4 cornerHi[];
} volumeCornerHi;

vec3 heat(float t)
{
  t = clamp(t, 0.0, 1.0);
  float r = smoothstep(0.5, 1.0, t);
  float g = t < 0.5 ? smoothstep(0.0, 0.5, t) : smoothstep(1.0, 0.5, t);
  float b = smoothstep(1.0, 0.5, t);
  return vec3(r, g, b);
}

int packedInt(ivec4 groups[4], int i)
{
  ivec4 g = groups[i / 4];
  return g[i & 3];
}

float packedFloat(vec4 groups[4], int i)
{
  vec4 g = groups[i / 4];
  return g[i & 3];
}

float gaussianComponent(float value, int i)
{
  float width = max(packedFloat(params.tfWidth, i), 1.0e-12);
  float center = packedFloat(params.tfCenter, i);
  float x = 0.0;
  if (packedInt(params.tfLogDomain, i) != 0) {
    if (value <= 0.0 || center <= 0.0) {
      return 0.0;
    }
    x = (log(max(value, 1.0e-30)) / log(10.0) -
         log(max(center, 1.0e-30)) / log(10.0)) / width;
  } else {
    x = (value - center) / width;
  }
  return packedFloat(params.tfAmp, i) * exp(-0.5 * x * x);
}

float transferNorm(float value)
{
  float lo = params.tfRangeScale.x;
  float hi = max(params.tfRangeScale.y, lo + 1.0e-6);
  float t = 0.0;
  if (params.tfControl.x != 0) {
    if (value <= 0.0 || lo <= 0.0) {
      return 0.0;
    }
    float llo = log(max(lo, 1.0e-30)) / log(10.0);
    float lhi = log(max(hi, 1.0e-30)) / log(10.0);
    t = (log(max(value, 1.0e-30)) / log(10.0) - llo) /
        max(lhi - llo, 1.0e-6);
  } else {
    t = (value - lo) / max(hi - lo, 1.0e-6);
  }
  return clamp(t, 0.0, 1.0);
}

float transferSigma(float value)
{
  float sigma = 0.0;
  int n = min(max(params.tfControl.y, 0), 16);
  for (int i = 0; i < n; ++i) {
    int type = packedInt(params.tfType, i);
    float center = packedFloat(params.tfCenter, i);
    float width = packedFloat(params.tfWidth, i);
    float amp = packedFloat(params.tfAmp, i);
    if (type == 0) {
      sigma += gaussianComponent(value, i);
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

vec3 volumeColor(float value)
{
  if (params.baseColorAndMode.w > 1.5) {
    float rows = max(float(params.colorControl.y), 1.0);
    float y = (float(max(params.colorControl.x, 0)) + 0.5) / rows;
    return texture(colormapAtlas, vec2(transferNorm(value), y)).rgb;
  }
  if (params.baseColorAndMode.w > 0.5) {
    return heat(transferNorm(value));
  }
  return params.baseColorAndMode.rgb;
}

float screenRadiusPx(float rEff, float zView, float focalPx)
{
  return (zView > 0.0) ? (focalPx * rEff / zView) : 1.0e9;
}

bool rayBox(vec3 ro, vec3 invd, vec3 mn, vec3 mx, inout float t0, inout float t1)
{
  vec3 t1v = (mn - ro) * invd;
  vec3 t2v = (mx - ro) * invd;
  vec3 tminv = min(t1v, t2v);
  vec3 tmaxv = max(t1v, t2v);
  float lo = max(max(tminv.x, tminv.y), tminv.z);
  float hi = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
  t0 = max(t0, lo);
  t1 = min(t1, hi);
  return t1 >= max(t0, 0.0);
}

float trilerp8(vec4 lo, vec4 hi, vec3 uvw)
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

void main()
{
  int root = params.tfControl.z;
  if (root < 0) {
    outColor = vec4(0.0);
    return;
  }

  vec2 ndc = vec2(inNdc.x, -inNdc.y);
  vec4 pN = params.invProj * vec4(ndc, -1.0, 1.0);
  pN /= pN.w;
  vec3 ro = (params.invView * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
  vec3 rd = normalize((params.invView * vec4(vec3(pN), 0.0)).xyz);
  vec3 invd = 1.0 / max(abs(rd), vec3(1.0e-30)) * sign(rd);

  vec3 rootMin = volumeNodeMin.nodeMin[root].xyz;
  vec3 rootMax = volumeNodeMax.nodeMax[root].xyz;
  float t0 = 0.0;
  float t1 = 1.0e30;
  if (!rayBox(ro, invd, rootMin, rootMax, t0, t1)) {
    outColor = vec4(0.0);
    return;
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
  vec3 color = vec3(0.0);

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

    vec4 nodeMin = volumeNodeMin.nodeMin[id];
    vec4 nodeMax = volumeNodeMax.nodeMax[id];
    vec3 bmin = nodeMin.xyz;
    vec3 bmax = nodeMax.xyz;

    float radius = 0.5 * length(bmax - bmin);
    vec3 center = 0.5 * (bmin + bmax);
    float zView = dot(center - ro, params.cameraForwardFocal.xyz);
    float rPx = screenRadiusPx(radius, zView, params.cameraForwardFocal.w);

    ivec4 cA = volumeChildA.childA[id];
    ivec4 cB = volumeChildB.childB[id];
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
      vec3 size = max(bmax - bmin, vec3(1.0e-8));
      int opticalModel = int(params.opticalParams.x + 0.5);
      for (int sampleIndex = 0; sampleIndex < 256 && sampleIndex < sampleCount;
           ++sampleIndex) {
        float ts = t0 + (float(sampleIndex) + 0.5) * dt;
        vec3 pmid = ro + rd * ts;
        vec3 uvw = clamp((pmid - bmin) / size, 0.0, 1.0);
        float value =
          trilerp8(volumeCornerLo.cornerLo[id],
                   volumeCornerHi.cornerHi[id],
                   uvw);
        float sigma = transferSigma(value);
        vec3 tfc = volumeColor(value);
        if (opticalModel == 0) {
          float a = 1.0 - exp(-sigma * dt);
          color += (1.0 - alpha) * a * tfc;
          alpha = 1.0 - (1.0 - alpha) * (1.0 - a);
        } else {
          float transmittance = 1.0 - alpha;
          float emission = sigma * max(params.opticalParams.y, 0.0);
          float absorption =
            (opticalModel == 2) ? sigma * max(params.opticalParams.z, 0.0) : 0.0;
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

    int childIdx[8] = int[8](
      cA.x, cA.y, cA.z, cA.w, cB.x, cB.y, cB.z, cB.w);

    int hitId[8];
    float hitT0[8];
    float hitT1[8];
    int hitCount = 0;

    for (int k = 0; k < 8; ++k) {
      int cid = childIdx[k];
      if (cid < 0) continue;
      vec3 childMin = volumeNodeMin.nodeMin[cid].xyz;
      vec3 childMax = volumeNodeMax.nodeMax[cid].xyz;
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
    outColor = vec4(heat(float(visits) / 100.0), 1.0);
    return;
  }
  if (params.tfControl.w == 11) {
    outColor = vec4(heat(clamp(float(leafStops) / 64.0, 0.0, 1.0)), 1.0);
    return;
  }
  if (params.tfControl.w == 12) {
    outColor = vec4(heat(clamp(float(lodStops) / 64.0, 0.0, 1.0)), 1.0);
    return;
  }
  if (params.tfControl.w == 13) {
    outColor = vec4(heat(clamp(float(emptySkips) / 64.0, 0.0, 1.0)), 1.0);
    return;
  }
  if (params.tfControl.w == 14) {
    outColor = vec4(vec3(alpha), 1.0);
    return;
  }
  if (params.tfControl.w == 20) {
    outColor = vec4(float(visits),
                    float(childHits),
                    float(leafStops),
                    1.0);
    return;
  }

  outColor = vec4(color, alpha);
}
