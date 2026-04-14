#version 450

/* Copyright (c) 2019-2026, Sascha Willems
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

layout (location = 0) in vec4 inPosition;   // xyz = position, w = life
layout (location = 1) in vec4 inVelocity;   // xyz = velocity, w = max_life
layout (location = 2) in vec4 inColor;      // rgba
layout (location = 3) in vec4 inMisc;       // x = size

layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 view;
	vec2 screen_dim;
	float particle_size;
} ubo;

layout (location = 0) out vec4 outColor;
layout (location = 1) out float outLife;

out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
};

void main()
{
	float life = inPosition.w;

	// Skip dead particles by moving them off-screen
	if (life <= 0.0)
	{
		gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
		gl_PointSize = 0.0;
		outColor = vec4(0.0);
		outLife = 0.0;
		return;
	}

	vec4 eye_pos = ubo.view * vec4(inPosition.xyz, 1.0);

	// Point size: Small value for "point" look
	float life_ratio = clamp(life / max(inVelocity.w, 0.001), 0.0, 1.0);
	gl_PointSize = max(1.0, ubo.particle_size * life_ratio);

	gl_Position = ubo.projection * eye_pos;

	outColor = inColor;
	outLife  = life_ratio;
}
