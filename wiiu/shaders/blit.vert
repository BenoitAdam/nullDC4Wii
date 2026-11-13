#version 450
// wiiu/shaders/blit.vert - draws the 640x480 Dreamcast frame onto the TV and
// the GamePad (plugs/drkPvr/gx2Rend.cpp present()).

layout(location = 0) in vec2 a_pos;   // NDC
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

void main()
{
  v_uv = a_uv;
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
