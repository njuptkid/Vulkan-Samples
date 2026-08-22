#version 450
/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Method B: mipmap + anisotropic fragment shader.
//
// Same rotation trajectory as Method A, but each tap uses textureGrad() with
// an explicit anisotropic gradient: the footprint is stretched along the
// rotation tangent (covering the per-step arc length) and kept at ~1 texel
// along the radial direction. The hardware anisotropic filter + mip chain then
// pre-filters along the tangent in a single fetch, so far fewer taps are needed
// for a continuous result.
//
// This is the whole point of the comparison: Method A pays for many texture()
// fetches; Method B lets the texture sampling hardware widen each footprint
// along the blur direction.

precision highp float;

layout(set = 0, binding = 0, std140) uniform Params
{
	vec2  u_center;
	float u_total_angle;
	uint  u_sample_count;
	float u_radius_scale;
}
params;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	vec2  d  = (in_uv - params.u_center) * params.u_radius_scale;
	float r  = length(d);
	float a0 = atan(d.y, d.x);

	// Tangent (rotation direction) and radial unit vectors at the sample angle.
	// For a point at angle a: tangent T = (-sin a, cos a), radial R = (cos a, sin a).
	vec2  texel = 1.0 / vec2(textureSize(color_sampler, 0));

	vec4  acc   = vec4(0.0);
	float total = float(params.u_sample_count);
	float inv_n = 1.0 / total;
	// Per-step arc length: how far along the tangent one tap should cover.
	float arc_step = r * params.u_total_angle * inv_n;

	for (uint i = 0u; i < params.u_sample_count; ++i)
	{
		float t = (float(i) + 0.5) * inv_n;
		float a = a0 + t * params.u_total_angle;

		float ca = cos(a);
		float sa = sin(a);
		vec2  s_uv = params.u_center + r * vec2(ca, sa);

		// Anisotropic gradient: stretch the footprint along the tangent so each
		// tap pre-filters exactly one arc-step; keep the radial direction at a
		// single texel so we don't blur across radii.
		vec2 tangent = vec2(-sa, ca);
		vec2 radial  = vec2(ca, sa);
		vec2 dPdx    = tangent * arc_step;
		vec2 dPdy    = radial  * texel;

		acc += textureGrad(color_sampler, s_uv, dPdx, dPdy);
	}
	acc /= total;

	out_color = acc;
}
