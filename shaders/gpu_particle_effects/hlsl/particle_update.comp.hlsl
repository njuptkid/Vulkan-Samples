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

struct Particle
{
	float4 position;    // xyz = position, w = life (remaining)
	float4 velocity;    // xyz = velocity, w = max_life
	float4 color;       // rgba
	float4 misc;        // x = size, yzw = unused
};

// Emitter parameters
struct EmitterParams
{
	float3  position;
	float   emit_rate;
	float3  direction;
	float   cone_angle;
	float3  gravity;
	float   min_life;
	float   max_life;
	float   min_speed;
	float   max_speed;
	int     emitter_type;
	float   time;
	float   delta_time;
	uint    particle_count;
	uint    seed;
};

[[vk::binding(0, 0)]]
RWStructuredBuffer<Particle> particles : register(u0);

[[vk::binding(1, 0)]]
ConstantBuffer<EmitterParams> emitter : register(b1);

// Dead particle counter (atomic)
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> dead_counter : register(u2);

[numthreads(128, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
	uint idx = GlobalInvocationID.x;
	if (idx >= emitter.particle_count)
		return;

	float life = particles[idx].position.w;
	if (life <= 0.0)
		return;

	// Delta time from UBO
	float dt = emitter.delta_time;

	// Apply gravity
	particles[idx].velocity.xyz += emitter.gravity * dt;

	// Integrate position
	particles[idx].position.xyz += particles[idx].velocity.xyz * dt;

	// Decrease life
	particles[idx].position.w -= dt;

	// If particle died, increment dead counter for recycling
	if (particles[idx].position.w <= 0.0)
	{
		particles[idx].position.w = 0.0;
		InterlockedAdd(dead_counter[0], 1u);
	}

	// Fade alpha based on remaining life ratio
	float life_ratio = particles[idx].position.w / particles[idx].velocity.w;
	life_ratio = clamp(life_ratio, 0.0, 1.0);

	// Smooth fade in/out
	float alpha = life_ratio;
	if (life_ratio > 0.8)
		alpha = (1.0 - life_ratio) * 5.0;
	particles[idx].color.a = alpha;
}
