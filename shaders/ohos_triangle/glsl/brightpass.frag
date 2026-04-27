#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bright-pass filter — extracts pixels above a luminance threshold.
 */

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConsts
{
	float threshold;
}
pc;

void main()
{
	vec4  color      = texture(color_sampler, in_uv);
	float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
	out_color = brightness > pc.threshold ? color : vec4(0.0);
}
