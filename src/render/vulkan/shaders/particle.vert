#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in uint inType;
layout(location = 2) in uint inStress;
layout(location = 3) in float inHsml;
layout(location = 4) in float inValue;

layout(set = 0, binding = 0) uniform VisualParams {
  mat4 mvp;
  vec4 pointSizesA;
  vec4 pointSizesB;
  vec4 valueMinA;
  vec4 valueMinB;
  vec4 valueMaxA;
  vec4 valueMaxB;
  uvec4 colormapA;
  uvec4 colormapB;
  uvec4 masks;
  vec4 fixedColor;
  vec4 renderParams;
} params;

layout(location = 0) out float outNormValue;
layout(location = 1) flat out uint outColormap;
layout(location = 2) out float outAlpha;
layout(location = 3) flat out float outAtlasRows;

void main()
{
  uint typeId = min(inType, 5u);
  if ((params.masks.x & (1u << typeId)) != 0u) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    gl_PointSize = 1.0;
    outNormValue = 0.0;
    outColormap = 0u;
    outAlpha = 0.0;
    outAtlasRows = params.renderParams.y;
    return;
  }

  vec4 pointSizes = (typeId < 4u) ? params.pointSizesA : params.pointSizesB;
  vec4 valueMin = (typeId < 4u) ? params.valueMinA : params.valueMinB;
  vec4 valueMax = (typeId < 4u) ? params.valueMaxA : params.valueMaxB;
  uvec4 colormap = (typeId < 4u) ? params.colormapA : params.colormapB;
  uint slot = typeId & 3u;

  float v = inValue;
  float lo = valueMin[slot];
  float hi = valueMax[slot];
  if ((params.masks.y & (1u << typeId)) != 0u) {
    v = log(max(v, 1.0e-30)) / log(10.0);
  }

  float denom = max(abs(hi - lo), 1.0e-30);
  float t = (v - lo) / denom;
  if ((params.masks.z & (1u << typeId)) != 0u) {
    t = fract(t);
  } else {
    t = clamp(t, 0.0, 1.0);
  }

  gl_Position = params.mvp * vec4(inPos, 1.0);
  gl_PointSize = max(pointSizes[slot] * params.renderParams.w, 1.0);
  outNormValue = t;
  outColormap = colormap[slot];
  outAlpha = params.renderParams.x;
  outAtlasRows = params.renderParams.y;
}
