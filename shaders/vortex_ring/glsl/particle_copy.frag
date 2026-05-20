#version 450

/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copy offscreen texture to swapchain using sampler.
 */

layout(binding = 0) uniform sampler2D inputColor;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;

void main()
{
    outFragColor = texture(inputColor, inUV);
}