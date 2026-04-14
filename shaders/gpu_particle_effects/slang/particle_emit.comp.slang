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

// Particle structure: 48 bytes
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
    float3 position;
    float   emit_rate;
    float3 direction;
    float   cone_angle;
    float3 gravity;
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

[[vk::binding(0, 0)]] RWStructuredBuffer<Particle> particles;
[[vk::binding(1, 0)]] ConstantBuffer<EmitterParams> emitter;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> dead_counter;

// PCG random number generator
uint pcg(uint state)
{
    uint word = ((state >> 22u) ^ state) * 4294967295u;
    return ((word >> 22u) ^ word) * 4294967295u;
}

float random_float(inout uint seed)
{
    seed = pcg(seed);
    return float(seed) / 4294967295.0;
}

float3 random_unit_vector(inout uint seed)
{
    float theta = 2.0 * 3.14159265 * random_float(seed);
    float z     = random_float(seed) * 2.0 - 1.0;
    float r     = sqrt(1.0 - z * z);
    return float3(r * cos(theta), r * sin(theta), z);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint rng_state = pcg(uint(emitter.time * 1000.0) + emitter.seed + GlobalInvocationID.x);

    // Each thread attempts to allocate one slot via atomic increment
    int idx;
    InterlockedAdd(dead_counter[0], 1u, uint(idx));
    idx -= 1;

    if (idx < 0 || idx >= int(emitter.particle_count))
        return;

    float life   = emitter.min_life + random_float(rng_state) * (emitter.max_life - emitter.min_life);
    float speed  = emitter.min_speed + random_float(rng_state) * (emitter.max_speed - emitter.min_speed);
    float3 vel   = float3(0.0, 0.0, 0.0);

    if (emitter.emitter_type == 0)
    {
        // Point emitter: random direction
        vel = random_unit_vector(rng_state) * speed;
    }
    else if (emitter.emitter_type == 1)
    {
        // Sphere emitter: random point on sphere, velocity = outward normal * speed
        vel = random_unit_vector(rng_state) * speed;
    }
    else
    {
        // Cone emitter: random direction within cone
        float3 dir = normalize(emitter.direction);
        float3 rnd = random_unit_vector(rng_state);
        // Rotate random vector toward cone axis
        float cos_angle = cos(emitter.cone_angle);
        float rnd_len   = length(rnd);
        if (rnd_len > 0.001)
        {
            rnd = normalize(rnd);
            // Slerp-like: blend toward direction based on cone angle
            float dot_rd = dot(rnd, dir);
            if (dot_rd < 0.0)
                rnd = -rnd;
            dot_rd = dot(rnd, dir);
            float t = lerp(1.0, cos_angle, random_float(rng_state));
            vel = normalize(lerp(rnd, dir, t)) * speed;
        }
        else
        {
            vel = dir * speed;
        }
    }

    // Color: warm gradient based on lifetime
    float t = (life - emitter.min_life) / max(emitter.max_life - emitter.min_life, 0.001);
    particles[idx].color = float4(
        lerp(1.0, 0.2, t),     // R
        lerp(0.6, 0.8, t),     // G
        lerp(0.1, 1.0, t),     // B
        1.0
    );

    particles[idx].position = float4(emitter.position, life);
    particles[idx].velocity = float4(vel, life);
    particles[idx].misc     = float4(lerp(8.0, 3.0, t), 0.0, 0.0, 0.0);
}
