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

struct VSInput
{
    float4 inPosition : TEXCOORD0;    // xyz = position, w = life
    float4 inVelocity : TEXCOORD1;    // xyz = velocity, w = max_life
    float4 inColor    : TEXCOORD2;    // rgba
    float4 inMisc     : TEXCOORD3;    // x = size
};

struct VSOutput
{
    float4 Pos       : SV_POSITION;
    float  PointSize : SV_PointSize;
    float4 outColor  : TEXCOORD0;
    float  outLife   : TEXCOORD1;
};

struct UBO
{
    float4x4 projection;
    float4x4 view;
    float2   screen_dim;
};
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;
    float life = input.inPosition.w;

    // Skip dead particles by moving them off-screen
    if (life <= 0.0)
    {
        output.Pos       = float4(0.0, 0.0, -2.0, 1.0);
        output.PointSize = 0.0;
        output.outColor  = float4(0.0, 0.0, 0.0, 0.0);
        output.outLife   = 0.0;
        return output;
    }

    float4 eye_pos = mul(ubo.view, float4(input.inPosition.xyz, 1.0));

    // Point size: scale with life ratio and perspective
    float life_ratio = clamp(life / max(input.inVelocity.w, 0.001), 0.0, 1.0);
    float sprite_size = input.inMisc.x * life_ratio;
    float4 projected_corner = mul(ubo.projection, float4(sprite_size, sprite_size, eye_pos.z, eye_pos.w));
    output.PointSize = clamp(ubo.screen_dim.x * projected_corner.x / projected_corner.w, 1.0, 64.0);

    output.Pos      = mul(ubo.projection, eye_pos);
    output.outColor = input.inColor;
    output.outLife  = life_ratio;

    return output;
}
