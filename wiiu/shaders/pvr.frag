#version 450
// wiiu/shaders/pvr.frag - PVR polygon pixel shader (plugs/drkPvr/gx2Rend.cpp).
// Compiled with CafeGLSL in register uniform mode (wiiu/Makefile).
//
// The four PVR shading instructions (TSP.ShadInstr) plus the offset colour.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 2) in vec4 v_spc;

layout(location = 0) out vec4 o_col;

layout(binding = 0) uniform sampler2D u_tex;

// x: textured (0/1)  y: shading instruction (0 decal, 1 modulate,
// 2 decal alpha, 3 modulate alpha)  z: ignore texture alpha  w: use vertex alpha
uniform vec4 u_mode;

void main()
{
  vec4 col = v_col;
  if (u_mode.w < 0.5)
    col.a = 1.0;

  if (u_mode.x < 0.5)
  {
    o_col = col;
    return;
  }

  vec4 t = texture(u_tex, v_uv);
  if (u_mode.z > 0.5)
    t.a = 1.0;

  vec4 r;
  if (u_mode.y < 0.5)
    r = t;                                        // decal
  else if (u_mode.y < 1.5)
    r = vec4(t.rgb * col.rgb, t.a);               // modulate
  else if (u_mode.y < 2.5)
    r = vec4(mix(col.rgb, t.rgb, t.a), col.a);    // decal alpha
  else
    r = t * col;                                  // modulate alpha

  r.rgb += v_spc.rgb;
  o_col = clamp(r, 0.0, 1.0);
}
