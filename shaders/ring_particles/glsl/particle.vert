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

// Instanced ring particle vertex shader (POINT mode).
//
// Each instance is a single point. Position is computed from gl_InstanceIndex;
// the per-pixel disc is drawn in the fragment shader using gl_PointCoord, so
// no quad geometry / vertex buffer is needed.
//
// Base ring position (user formula):
//   angle          = (instanceId % ringParticleNum) / ringParticleNum * 2*PI
//   positionOffset = (cos(angle), sin(angle), 0)
//   positionOffset *= ringRadius * particleScale
//
// Perlin-noise position displacement (user formula):
//   flow            = iTime * (1,0,0.3) * timeFrequencies
//   coord           = initPos * locationFrequencies + flow + seed
//   positionOffset.x += perlinNoise3D(coord + 10086) * displace * particlePositionNoise
//   positionOffset.y += perlinNoise3D(coord + 666)   * displace * particlePositionNoise
//   positionOffset.z += perlinNoise3D(coord)          * displace * particlePositionNoise
//
// Perlin-noise size modulation (user formula, three steps), then converted to
// pixels via pointSizeScale and written to gl_PointSize.

precision highp float;

layout(set = 0, binding = 0, std140) uniform UBO
{
	mat4 projection;
	mat4 view;
	float ringRadius;              // base ring radius
	float particleScale;           // scales positionOffset (user formula)
	float iparticleSizeInstance;    // base particle size (user formula)
	float time;                     // elapsed time (spin + noise flow)
	uint  ringParticleNum;          // number of particles on the ring
	float locationFrequencies;      // noise spatial frequency (0.8)
	float timeFrequencies;          // noise temporal frequency
	float displace;                 // noise displacement base (0.4)
	float particlePositionNoise;    // noise displacement gain (0.6)
	float ringWidth1;               // ring width factor for size (1.0)
	float sizeRate;                 // size noise rate (3)
	float particleReformNoise;      // size noise gain (0.32)
	float pointSizeScale;           // world-size -> pixel conversion
}
ubo;

layout(location = 0) out vec3 outColor;

// --- Classic Perlin 3D noise (Ken Perlin's gradient noise) ---
// Each integer lattice point gets a pseudo-random gradient via a hash; the
// 8 corner gradients are dotted with the distance to the sample point and
// combined with a quintic (Perlin) fade trilinear interpolation. This is the
// classic (pre-simplex) Perlin algorithm, range roughly [-1, 1].

vec3 hash33(vec3 p)
{
	// Integer-position hash (NO sin). sin is ~30 cycles on most GPUs; this
	// PCG-style integer hash is ~5 cycles. Called 8x per perlinNoise3D, and
	// each particle runs ~3 perlin calls, so this is the dominant vertex cost.
	uvec3 q = uvec3(ivec3(p));
	q = q * 1664525u + 1013904223u;
	q.x += q.y * q.z; q.y += q.z * q.x; q.z += q.x * q.y;
	q ^= q >> 16u;
	q.x += q.y * q.z; q.y += q.z * q.x; q.z += q.x * q.y;
	q ^= q >> 16u;
	return -1.0 + 2.0 * (vec3(q) * (1.0 / 4294967296.0));
}

float perlinNoise3D(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);

	// Quintic fade (Perlin's improved fade: 6t^5 - 15t^4 + 10t^3).
	vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

	// 8 corner gradients dotted with the distance from the corner to P.
	float n000 = dot(hash33(i + vec3(0.0, 0.0, 0.0)), f - vec3(0.0, 0.0, 0.0));
	float n100 = dot(hash33(i + vec3(1.0, 0.0, 0.0)), f - vec3(1.0, 0.0, 0.0));
	float n010 = dot(hash33(i + vec3(0.0, 1.0, 0.0)), f - vec3(0.0, 1.0, 0.0));
	float n110 = dot(hash33(i + vec3(1.0, 1.0, 0.0)), f - vec3(1.0, 1.0, 0.0));
	float n001 = dot(hash33(i + vec3(0.0, 0.0, 1.0)), f - vec3(0.0, 0.0, 1.0));
	float n101 = dot(hash33(i + vec3(1.0, 0.0, 1.0)), f - vec3(1.0, 0.0, 1.0));
	float n011 = dot(hash33(i + vec3(0.0, 1.0, 1.0)), f - vec3(0.0, 1.0, 1.0));
	float n111 = dot(hash33(i + vec3(1.0, 1.0, 1.0)), f - vec3(1.0, 1.0, 1.0));

	// Trilinear interpolation along the faded weights.
	float nx00 = mix(n000, n100, u.x);
	float nx10 = mix(n010, n110, u.x);
	float nx01 = mix(n001, n101, u.x);
	float nx11 = mix(n011, n111, u.x);

	float nxy0 = mix(nx00, nx10, u.y);
	float nxy1 = mix(nx01, nx11, u.y);

	return mix(nxy0, nxy1, u.z);
}

// Per-instance pseudo-random in [0,1) (classic hash).
float random(float n)
{
	return fract(sin(n) * 43758.5453);
}

const float TWO_PI = 6.28318530718;

void main()
{
	uint  id    = uint(gl_InstanceIndex);
	uint  ring  = ubo.ringParticleNum;
	float angle = float(id % ring) / float(ring) * TWO_PI;
	angle += ubo.time;                              // spin the distribution

	// --- Base ring position (user formula) ---
	// initPos is the UNIT ring direction (cos, sin, 0) with NO scaling. It is
	// used for the noise coordinates so the noise sampling space is
	// independent of ringRadius / particleScale.
	vec3  initPos = vec3(cos(angle), sin(angle), 0.0);
	float seed    = float(id);
	vec3  flow     = ubo.time * vec3(1.0, 0.0, 0.3) * ubo.timeFrequencies;

	// --- Perlin-noise RADIAL modulation ---
	// NOTE: `seed` (= float(id)) is NOT added to the POSITION noise coord.
	// Adding it makes adjacent particles' coord jump by 1/id -> radius jumps
	// -> radial scatter -> wide band. Without seed, coord varies smoothly with
	// the angle, so neighboring particles get similar radii and aggregate
	// into a narrow distorted ring.
	vec3  coord  = initPos * ubo.locationFrequencies + flow;
	float amp    = ubo.displace * ubo.particlePositionNoise;
	float radius = ubo.ringRadius * ubo.particleScale + perlinNoise3D(coord) * amp;
	vec3  positionOffset = vec3(cos(angle), sin(angle), 0.0) * radius;
	positionOffset.z += perlinNoise3D(coord + 777.0) * amp;

	// --- Perlin-noise size modulation (user formula, three steps) ---
	float particleSize = ubo.iparticleSizeInstance * 0.115 * ubo.ringWidth1;
	particleSize = mix(particleSize * 0.5, particleSize, random(seed));
	particleSize *= perlinNoise3D(initPos + flow + seed + 11223.0)
	                * ubo.sizeRate * 5.0 * ubo.particleReformNoise + 1.0;
	particleSize = max(particleSize, 0.001);

	// --- Point output: position + pixel size ---
	gl_Position  = ubo.projection * ubo.view * vec4(positionOffset, 1.0);
	// Convert the (world-unit) particleSize to framebuffer pixels and clamp to
	// the device's supported point size range ([1, 64] is a safe default).
	gl_PointSize = clamp(particleSize * ubo.pointSizeScale, 1.0, 64.0);

	// Color by angle for a rainbow ring so the distribution is easy to inspect.
	float h = angle / TWO_PI;
	outColor = 0.5 + 0.5 * cos(6.2831853 * (h + vec3(0.0, 0.33, 0.67)));
}
