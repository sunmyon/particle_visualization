#version 450

layout(set = 0, binding = 1) uniform sampler2D colormapAtlas;

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

layout(location = 0) in float inNormValue;
layout(location = 1) flat in uint inColormap;
layout(location = 2) in float inAlpha;
layout(location = 3) flat in float inAtlasRows;
layout(location = 0) out vec4 outColor;

void main()
{
  vec2 p = gl_PointCoord * 2.0 - vec2(1.0);
  if (dot(p, p) > 1.0) {
    discard;
  }

  float atlasRows = max(inAtlasRows, 1.0);
  float y = (float(inColormap) + 0.5) / atlasRows;
  vec3 color = (params.renderParams.z > 0.5)
    ? params.fixedColor.rgb
    : texture(colormapAtlas, vec2(clamp(inNormValue, 0.0, 1.0), y)).rgb;
  float alpha = (params.renderParams.z > 0.5) ? params.fixedColor.a : inAlpha;
  outColor = vec4(color, alpha);
}
