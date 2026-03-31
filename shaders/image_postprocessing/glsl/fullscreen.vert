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

// Fullscreen quad vertex shader
// Outputs a quad covering the entire screen with UV coordinates

layout(location = 0) out vec2 out_uv;

void main()
{
	// Generate fullscreen quad vertices
	// gl_VertexIndex: 0, 1, 2, 3, 4, 5 (two triangles)
	// UV mapping: (0,0) -> (1,1)
	vec2 uv = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
	out_uv = uv;

	// Convert UV to clip space: (0,0) -> (-1,-1), (1,1) -> (1,1)
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);

	// Flip Y for Vulkan coordinate system
	gl_Position.y = -gl_Position.y;
}
