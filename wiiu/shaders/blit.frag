#version 450
// wiiu/shaders/blit.frag - samples the offscreen Dreamcast frame.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col;

layout(binding = 0) uniform sampler2D u_tex;

void main()
{
  o_col = vec4(texture(u_tex, v_uv).rgb, 1.0);
}
