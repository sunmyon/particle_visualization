#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in float inOpacity;
layout(location = 0) out vec4 outColor;

void main()
{
  if (inOpacity <= 0.0) {
    discard;
  }
  outColor = vec4(inColor, inOpacity);
}
