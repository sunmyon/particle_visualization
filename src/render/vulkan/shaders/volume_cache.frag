#version 450

layout(set = 0, binding = 0) uniform sampler2D cacheImage;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
  vec2 uv = inNdc * 0.5 + vec2(0.5);
  outColor = texture(cacheImage, uv);
}
