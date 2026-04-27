#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Line Integral Convolution (LIC) — direction-based blur.
 * Uses luminance from a blurred "map" texture to determine blur direction,
 * then performs weighted sampling along that direction.
 *
 * Reference: WebGPU Fluid Simulation VectorBlurEffect
 */

layout(set = 0, binding = 2) uniform sampler2D color_sampler;
layout(set = 0, binding = 3) uniform sampler2D map_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConsts
{
	float amount;      // number of LIC samples (0 = passthrough)
	float res_x;       // screen width in pixels
	float res_y;       // screen height in pixels
	float revolution;  // direction rotation multiplier
}
pc;

float luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	int amount = int(pc.amount);
	if (amount == 0)
	{
		out_color = texture(color_sampler, in_uv);
		return;
	}

	// Sample blurred map for smooth direction field
	vec3  map_color = texture(map_sampler, in_uv).rgb;
	float lum       = luminance(map_color);

	// Map luminance to angle [0, 2PI] scaled by revolution
	float angle = lum * 6.28318530718 * pc.revolution;
	vec2  dir   = vec2(cos(angle), sin(angle));

	vec2 step_size      = vec2(1.0 / pc.res_x, 1.0 / pc.res_y);
	vec2 uv_offset_step = dir * step_size;

	vec4  composite   = vec4(0.0);
	float total_weight = 0.0;

	// Single-direction forward LIC with parabolic weight falloff
	for (int i = 0; i <= amount; i++)
	{
		float x    = float(i);
		float norm = x / float(amount);
		float weight = 1.0 - (norm * norm);

		composite += texture(color_sampler, in_uv + uv_offset_step * x) * weight;
		total_weight += weight;
	}

	out_color = composite / total_weight;
}
