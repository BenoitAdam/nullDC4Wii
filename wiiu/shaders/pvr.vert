#version 450
// wiiu/shaders/pvr.vert - PVR polygon vertex shader (plugs/drkPvr/gx2Rend.cpp).
// Compiled with CafeGLSL in register uniform mode (wiiu/Makefile).
//
// Input is the raw TA vertex: x/y in canvas pixels, z = 1/W. The output keeps
// w = W so the GPU interpolates uv and colours perspective-correctly, and
// writes depth f(1/W) = (1/W) / (1 + 1/W): monotonic in 1/W, inside (0, 1),
// so the PVR's depth compares (made on 1/W) keep their meaning as-is.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;   // base colour, RGBA8
layout(location = 3) in vec4 a_spc;   // offset colour, RGBA8 (alpha unused)

// xy: canvas pixel -> NDC scale, zw: NDC offset
uniform vec4 u_xform;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec4 v_spc;

void main()
{
  float invw = max(a_pos.z, 1.0e-6);   // guard 0 / negative / NaN-ish 1/W
  float w    = 1.0 / invw;
  vec2  ndc  = a_pos.xy * u_xform.xy + u_xform.zw;
  float d    = invw / (1.0 + invw);
  gl_Position = vec4(ndc * w, d * w, w);

  v_uv  = a_uv;
  v_col = a_col;
  v_spc = a_spc;
}
