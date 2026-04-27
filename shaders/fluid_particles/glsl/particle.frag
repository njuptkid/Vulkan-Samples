#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Particle fragment shader.
 * Circular particle with smoothstep alpha falloff and premultiplied alpha output.
 */

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outFragColor;

void main()
{
	// Circular falloff using UV distance from center
	float dist  = length(vUV);
	float alpha = 1.0 - smoothstep(0.5, 1.0, dist);

	if (alpha < 0.01)
		discard;

	// Premultiplied alpha: color.rgb * alpha for correct blending with src=ONE
	outFragColor = vec4(vColor.rgb * vColor.a * alpha, vColor.a * alpha);
}
