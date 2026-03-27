#version 450
/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License");
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

// Luminance gradient computation fragment shader
// Computes luminance gradients and stores X in R channel, Y in G channel
// Output: R = gradient X (horizontal), G = gradient Y (vertical), B = 0, A = 1

precision highp float;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

// Sobel kernels for gradient computation
// Gx = [[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]]  - horizontal gradient (detects vertical edges)
// Gy = [[-1,-2,-1], [0, 0, 0], [1, 2, 1]]    - vertical gradient (detects horizontal edges)

// ITU-R BT.709 luminance coefficients
const vec3 LUMINANCE_WEIGHTS = vec3(0.2126, 0.7152, 0.0722);

float luminance(vec3 color)
{
	return dot(color, LUMINANCE_WEIGHTS);
}

void main()
{
	vec2 texel_size = 1.0 / vec2(textureSize(color_sampler, 0));

	// Sample 3x3 neighborhood and compute luminance
	// Layout:
	//   tl  tc  tr
	//   ml  mc  mr
	//   bl  bc  br

	float tl = luminance(texture(color_sampler, in_uv + texel_size * vec2(-1, -1)).rgb);
	float tc = luminance(texture(color_sampler, in_uv + texel_size * vec2( 0, -1)).rgb);
	float tr = luminance(texture(color_sampler, in_uv + texel_size * vec2( 1, -1)).rgb);
	float ml = luminance(texture(color_sampler, in_uv + texel_size * vec2(-1,  0)).rgb);
	// mc not used in Sobel
	float mr = luminance(texture(color_sampler, in_uv + texel_size * vec2( 1,  0)).rgb);
	float bl = luminance(texture(color_sampler, in_uv + texel_size * vec2(-1,  1)).rgb);
	float bc = luminance(texture(color_sampler, in_uv + texel_size * vec2( 0,  1)).rgb);
	float br = luminance(texture(color_sampler, in_uv + texel_size * vec2( 1,  1)).rgb);

	// Sobel gradients
	// Gx: horizontal gradient (positive = brighter on the right)
	float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
	// Gy: vertical gradient (positive = brighter on the bottom)
	float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;

	// Normalize gradients from [-4, 4] to [0, 1] range
	// Sobel output range is approximately [-4, 4] for 8-bit input
	float gx_normalized = gx * 0.125 + 0.5;  // gx / 8 + 0.5
	float gy_normalized = gy * 0.125 + 0.5;  // gy / 8 + 0.5

	// Store gradient X in R channel, gradient Y in G channel
	// B channel = 0, A channel = 1
	out_color = vec4(gx_normalized, gy_normalized, 0.0, 1.0);
}
