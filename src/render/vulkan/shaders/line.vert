#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform LineParams {
  mat4 view;
  mat4 projection;
} params;

layout(push_constant) uniform LinePush {
  float opacityScale;
} pushData;

layout(location = 0) out vec4 outColor;

void main()
{
  gl_Position = params.projection * params.view * vec4(inPos, 1.0);
  outColor = vec4(inColor.rgb, clamp(inColor.a * pushData.opacityScale, 0.0, 1.0));
}
