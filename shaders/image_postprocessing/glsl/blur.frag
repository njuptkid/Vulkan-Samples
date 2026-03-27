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

// Gaussian blur fragment shader (5x5 kernel)

precision highp float;

layout(set = 0, binding = 2) uniform sampler2D color_sampler;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	vec2 texel_size = 1.0 / vec2(textureSize(color_sampler, 0));

	// Separable Gaussian blur weights (sigma ~ 1.5)
	// Normalized weights for a 9-tap 1D kernel (indices 0-4)
	// weights[0] = center, weights[1-4] = offsets 1-4
	float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

	// Single-pass 2D Gaussian blur using separable kernel
	// 2D Gaussian = horizontal_1D * vertical_1D
	vec4 result = vec4(0.0);
	for (int j = -4; j <= 4; ++j)
	{
		float weight_y = weights[abs(j)];
		for (int i = -4; i <= 4; ++i)
		{
			float weight_x = weights[abs(i)];
			vec2 offset = texel_size * vec2(float(i), float(j));
			// 2D weight is product of two 1D weights (separable property)
			result += texture(color_sampler, in_uv + offset) * weight_x * weight_y;
		}
	}

	out_color = result;
}
