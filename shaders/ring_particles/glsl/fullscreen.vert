#version 450
/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fullscreen triangle (no vertex buffer). gl_VertexIndex -> clip-space
 * position + UV in [0,1]. Used by the rotation-blur post-process pass.
 */

precision highp float;

layout(location = 0) out vec2 vUV;

void main()
{
	// One large triangle covering the whole screen.
	vec2 positions[3] = vec2[3](
		vec2(-1.0, -1.0),
		vec2( 3.0, -1.0),
		vec2(-1.0,  3.0)
	);
	vec2 uvs[3] = vec2[3](
		vec2(0.0, 0.0),
		vec2(2.0, 0.0),
		vec2(0.0, 2.0)
	);
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	vUV          = uvs[gl_VertexIndex];
}
