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

struct PSInput
{
    float4 inColor : TEXCOORD0;
    float  inLife  : TEXCOORD1;
    float2 PointCoord : SV_PointCoord;
};

[shader("fragment")]
float4 main(PSInput input) : SV_Target
{
    // Circular point sprite mask
    float dist = length(input.PointCoord - float2(0.5, 0.5));
    if (dist > 0.5)
        discard;

    // Soft edge falloff
    float alpha = 1.0 - smoothstep(0.2, 0.5, dist);

    // Brighter center glow
    float glow = exp(-dist * dist * 8.0);

    return float4(input.inColor.rgb * (alpha + glow * 0.5), input.inColor.a * alpha);
}
