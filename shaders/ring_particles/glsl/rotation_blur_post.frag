#version 450
/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Rotation-blur post-process fragment shader. Reads the offscreen Color
 * attachment (particle render) and, per output pixel, samples N points along
 * an arc centered on the pixel's polar angle about a rotation center.
 *
 * IMPORTANT: the arc is computed in PIXEL space (not UV space). UV space is
 * non-isotropic when the window aspect != 1 (X and Y texel pitch differ), so
 * sampling in UV space yields an elliptical blur pattern. Converting to pixels
 * via textureSize() keeps the arc circular in screen space.
 */

precision highp float;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(set = 0, binding = 1, std140) uniform Params
{
	vec2  u_center;       // rotation center in UV [0,1]
	float u_total_angle;   // total arc sweep in radians
	uint  u_samples;       // number of taps
}
params;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	// Work in pixel space so the arc is circular regardless of window aspect.
	vec2 res       = vec2(textureSize(srcTex, 0));
	vec2 center_px = params.u_center * res;
	vec2 d_px       = (vUV - params.u_center) * res;   // pixel-space offset
	float r         = length(d_px);
	float a0        = atan(d_px.y, d_px.x);

	// Adaptive sample count: keep adjacent taps <= 1 pixel apart along the arc
	// (arc length in pixels = r * total_angle). The center (r ~ 0) is the
	// rotation pivot and barely moves, so N=1 there (passthrough); the edges
	// where the arc is long get more taps. Capped at u_samples (the GUI slider).
	// This cuts the average tap count far below the fixed-N case.
	float arc = r * params.u_total_angle;
	uint  N   = uint(clamp(ceil(arc), 1.0, float(params.u_samples)));

	vec4  acc   = vec4(0.0);
	float inv_n = 1.0 / float(N);
	for (uint i = 0u; i < N; ++i)
	{
		float t = (float(i) + 0.5) * inv_n;          // [0,1), centered
		float a = a0 + t * params.u_total_angle;      // single direction
		// Sample point in pixel space, then back to UV for the texture fetch.
		vec2 s_px  = center_px + r * vec2(cos(a), sin(a));
		vec2 s_uv  = s_px / res;
		acc += texture(srcTex, s_uv);
	}
	outColor = acc * inv_n;
}
