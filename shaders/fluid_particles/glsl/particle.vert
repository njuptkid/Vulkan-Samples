#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Particle billboard vertex shader.
 * Reads particle data from SSBOs, renders instanced quads with perspective scaling.
 */

layout(location = 0) in vec2 inQuadPos;

layout(std430, binding = 0) readonly buffer Positions
{
	float positions[];
};

layout(std430, binding = 1) readonly buffer Life
{
	float life[];
};

layout(std430, binding = 2) readonly buffer Colors
{
	float colors[];
};

layout(binding = 3) uniform UBO
{
	mat4 view_proj;
	float particle_size;
}
ubo;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main()
{
	uint index = uint(gl_InstanceIndex);
	uint base  = index * 3u;

	float l = life[index];

	// Dead particle: move off-screen
	if (l <= 0.0)
	{
		gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
		vColor      = vec4(0.0);
		vUV         = vec2(0.0);
		return;
	}

	// Read particle position (stride-3)
	vec3 pos = vec3(positions[base], positions[base + 1u], positions[base + 2u]);

	// Transform to clip space
	vec4 clip_pos = ubo.view_proj * vec4(pos, 1.0);

	// Perspective scaling: farther particles appear smaller
	float perspective_scale = 1.0 / max(clip_pos.w, 0.001);

	// Billboard: offset in clip space
	gl_Position = clip_pos + vec4(inQuadPos * ubo.particle_size * perspective_scale, 0.0, 0.0);

	// Read particle color (stride-3)
	vec3 birth_color = vec3(colors[base], colors[base + 1u], colors[base + 2u]);

	// Alpha modulated by life (fade out as particle dies)
	vColor = vec4(birth_color, 1.0);
	vUV    = inQuadPos;
}
