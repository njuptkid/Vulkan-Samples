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

// Round point-sprite fragment shader. gl_PointCoord gives the in-quad
// coordinate [0,1]x[0,1] of the point; convert to centered [-1,1] and discard
// outside the unit circle, with a soft edge for anti-aliasing.

precision highp float;

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outFragColor;

void main()
{
	// gl_PointCoord in [0,1] -> centered [-1,1]
	vec2  d  = gl_PointCoord * 2.0 - 1.0;
	float r2 = dot(d, d);          // squared distance from disc center

	if (r2 > 1.0)
	{
		discard;
	}

	// Soft edge over the last ~15% of the radius for anti-aliasing.
	float alpha = 1.0 - smoothstep(0.85, 1.0, r2);
	outFragColor = vec4(inColor, alpha);
}
