#version 450

layout(location = 0) in vec3 inPos;

layout(location = 1) in vec4 inModel0;
layout(location = 2) in vec4 inModel1;
layout(location = 3) in vec4 inModel2;
layout(location = 4) in vec4 inModel3;
layout(location = 5) in vec3 inColor;
layout(location = 6) in float inOpacity;

layout(set = 0, binding = 0) uniform SolidParams {
  mat4 view;
  mat4 projection;
} params;

layout(push_constant) uniform SolidPush {
  float opacityScale;
} pushData;

layout(location = 0) out vec3 outColor;
layout(location = 1) out float outOpacity;

void main()
{
  mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
  gl_Position = params.projection * params.view * model * vec4(inPos, 1.0);
  outColor = inColor;
  outOpacity = clamp(inOpacity * pushData.opacityScale, 0.0, 1.0);
}
