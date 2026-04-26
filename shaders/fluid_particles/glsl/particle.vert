#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Particle vertex shader for instanced quad rendering.
 * Reads per-particle position, life, color from SSBOs.
 */

layout(location = 0) in vec2 inQuadPos;

layout(std430, binding = 0) readonly buffer PosBuffer
{
	float pos[];
};

layout(std430, binding = 1) readonly buffer LifeBuffer
{
	float life[];
};

layout(std430, binding = 2) readonly buffer ColorBuffer
{
	float color[];
};

layout(binding = 3) uniform UBO
{
	mat4  view_proj;
	float particle_size;
};

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outQuadUV;

void main()
{
	uint idx = gl_InstanceIndex;

	float l = life[idx];
	if (l <= 0.0)
	{
		// Dead particle: off-screen
		gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
		outColor = vec4(0.0);
		outQuadUV = vec2(0.5);
		return;
	}

	uint base = idx * 3u;
	vec3 p = vec3(pos[base], pos[base + 1u], pos[base + 2u]);

	// Transform to clip space
	vec4 clip = view_proj * vec4(p, 1.0);

	// Scale quad by particle_size in NDC
	float sz = particle_size * clip.w;
	clip.xy += inQuadPos * sz;

	gl_Position = clip;

	// UV: map quadPos (-1..1) to (0..1)
	outQuadUV = inQuadPos * 0.5 + 0.5;

	// Color with alpha based on remaining life
	float alpha = clamp(l * 0.8, 0.0, 1.0);
	outColor = vec4(color[base], color[base + 1u], color[base + 2u], alpha);
}
