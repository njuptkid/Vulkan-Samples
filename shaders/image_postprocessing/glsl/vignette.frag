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

// Vignette effect fragment shader

precision highp float;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	vec4 color = texture(color_sampler, in_uv);

	// Calculate distance from center (0.5, 0.5)
	vec2 center = vec2(0.5, 0.5);
	vec2 uv = in_uv - center;
	float dist = length(uv);

	// Vignette intensity (adjustable)
	float intensity = 0.8;
	float radius = 0.5;

	// Smooth vignette falloff
	float vignette = smoothstep(radius, radius - 0.3, dist * intensity);

	out_color = vec4(color.rgb * vignette, color.a);
}
