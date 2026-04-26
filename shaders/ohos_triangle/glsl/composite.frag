#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Composite pass — blends original scene with blurred glow.
 */

layout(set = 0, binding = 2) uniform sampler2D scene_sampler;
layout(set = 0, binding = 3) uniform sampler2D glow_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConsts
{
	float glow_intensity;
}
pc;

void main()
{
	vec4 scene = texture(scene_sampler, in_uv);
	vec4 glow  = texture(glow_sampler, in_uv);
	out_color  = scene + glow * pc.glow_intensity;
}
