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

// Gradient computation fragment shader using Sobel operator
// Calculates image gradients and visualizes edge detection

precision highp float;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

// Sobel kernels for gradient computation
// Gx = [[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]]  - horizontal gradient
// Gy = [[-1,-2,-1], [0, 0, 0], [1, 2, 1]]    - vertical gradient

void main()
{
	vec2 texel_size = 1.0 / vec2(textureSize(color_sampler, 0));

	// Sample 3x3 neighborhood and compute luminance
	float tl = dot(texture(color_sampler, in_uv + texel_size * vec2(-1, -1)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float tc = dot(texture(color_sampler, in_uv + texel_size * vec2( 0, -1)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float tr = dot(texture(color_sampler, in_uv + texel_size * vec2( 1, -1)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float ml = dot(texture(color_sampler, in_uv + texel_size * vec2(-1,  0)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float mr = dot(texture(color_sampler, in_uv + texel_size * vec2( 1,  0)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float bl = dot(texture(color_sampler, in_uv + texel_size * vec2(-1,  1)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float bc = dot(texture(color_sampler, in_uv + texel_size * vec2( 0,  1)).rgb, vec3(0.2126, 0.7152, 0.0722));
	float br = dot(texture(color_sampler, in_uv + texel_size * vec2( 1,  1)).rgb, vec3(0.2126, 0.7152, 0.0722));

	// Sobel gradients
	float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
	float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;

	// Gradient magnitude
	float magnitude = sqrt(gx*gx + gy*gy);

	// Normalize and output
	// Use a color gradient for better visualization
	vec3 edge_color = vec3(magnitude);

	// Optional: Colorize gradient direction
	// float angle = atan(gy, gx);
	// edge_color = vec3(cos(angle) * 0.5 + 0.5, sin(angle) * 0.5 + 0.5, magnitude);

	out_color = vec4(edge_color, 1.0);
}
