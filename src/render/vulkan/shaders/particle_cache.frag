#version 450

layout(set = 0, binding = 0) uniform sampler2D cacheColor;
layout(set = 0, binding = 1) uniform sampler2D cacheDepth;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
  vec2 uv = inNdc * 0.5 + vec2(0.5);
  vec4 color = texture(cacheColor, uv);
  if (color.a <= 0.0) {
    discard;
  }
  outColor = color;
  gl_FragDepth = texture(cacheDepth, uv).r;
}
