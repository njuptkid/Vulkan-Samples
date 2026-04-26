#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Particle fragment shader — soft circle with premultiplied alpha.
 * Used with instanced quad rendering (triangle list, not point sprites).
 */

layout(location = 0) in vec4  inColor;
layout(location = 1) in vec2  inQuadUV;

layout(location = 0) out vec4 outFragColor;

void main()
{
	// inQuadUV is 0..1 across the quad; convert to -1..1
	vec2  dxy  = inQuadUV * 2.0 - 1.0;
	float dist = dot(dxy, dxy);
	if (dist > 1.0)
		discard;

	float alpha = inColor.a * (1.0 - sqrt(dist));

	// Premultiplied alpha output
	outFragColor = vec4(inColor.rgb * alpha, alpha);
}
