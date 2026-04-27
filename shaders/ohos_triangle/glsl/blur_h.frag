#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Horizontal 1D Gaussian blur (separable).
 * Uses bilinear-weighted sampling for efficiency.
 */

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	vec2 texel_size = 1.0 / vec2(textureSize(color_sampler, 0));

	// Separable Gaussian weights (sigma ~ 4.0)
	float weights[5] = float[](0.204164, 0.180842, 0.123322, 0.065390, 0.026976);
	float offsets[5] = float[](0.0, 3.824, 7.602, 11.290, 14.834);

	vec4 result = vec4(0.0);
	for (int i = 0; i < 5; ++i)
	{
		float w   = weights[i];
		float off = offsets[i];
		result += texture(color_sampler, in_uv + vec2(off, 0.0) * texel_size) * w;
		result += texture(color_sampler, in_uv - vec2(off, 0.0) * texel_size) * w;
	}
	out_color = result;
}
