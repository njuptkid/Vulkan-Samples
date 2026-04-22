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

// Gaussian blur fragment shader (large radius, sigma ~ 4.0)

precision highp float;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	vec2 texel_size = 1.0 / vec2(textureSize(color_sampler, 0));

	// Separable Gaussian blur weights (sigma ~ 4.0)
	// 9-tap 1D kernel with large step (4 texels between taps)
	// Effective kernel radius = 4 * 4 = 16 texels per side
	float weights[5] = float[](0.204164, 0.180842, 0.123322, 0.065390, 0.026976);

	// Offsets for bilinear-weighted sampling (step = 4 texels)
	float offsets[5] = float[](0.0, 3.824, 7.602, 11.290, 14.834);

	// Single-pass 2D Gaussian blur
	vec4 result = vec4(0.0);
	for (int j = 0; j < 5; ++j)
	{
		float weight_y = weights[j];
		float off_y = offsets[j];
		for (int i = 0; i < 5; ++i)
		{
			float weight_x = weights[i];
			float off_x = offsets[i];

			float w = weight_x * weight_y;

			// Sample 4 symmetric positions using bilinear optimization
			result += texture(color_sampler, in_uv + vec2( off_x,  off_y) * texel_size) * w;
			result += texture(color_sampler, in_uv + vec2(-off_x,  off_y) * texel_size) * w;
			result += texture(color_sampler, in_uv + vec2( off_x, -off_y) * texel_size) * w;
			result += texture(color_sampler, in_uv + vec2(-off_x, -off_y) * texel_size) * w;
		}
	}

	out_color = result;
}
