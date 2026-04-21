/* Copyright (c) 2024, Huawei Technologies Co., Ltd.
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

#pragma once

#include "vulkan_sample.h"

#include "core/buffer.h"
#include "core/hpp_pipeline.h"
#include "core/hpp_pipeline_layout.h"
#include "core/hpp_render_pass.h"
#include "core/hpp_shader_module.h"
#include "rendering/render_target.h"
#include <vk_mem_alloc.h>

/**
 * @brief A triangle sample using VulkanSampleCpp framework template.
 *
 * VulkanSample::prepare() handles Instance/Device/Surface/RenderContext/Swapchain
 * creation automatically. VulkanSample::update() handles the frame lifecycle
 * (acquire, begin cmd, stats, draw, end cmd, submit).
 *
 * This sample overrides draw_renderpass() to handle render pass begin/end
 * and draw the triangle. The parent's draw() handles image barriers.
 */
class OHOSTriangle : public vkb::VulkanSampleCpp
{
	struct Vertex
	{
		float pos[3];
		float color[3];
	};

  public:
	OHOSTriangle()          = default;
	~OHOSTriangle() override = default;

	bool prepare(const vkb::ApplicationOptions &options) override;

	/// @brief Make validation layers optional (not installed on OHOS devices).
	void request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const override;

	/// @brief Handles render pass begin/end and triangle draw.
	///        Image barriers are handled by the parent's draw().
	void draw_renderpass(vkb::core::CommandBufferCpp &command_buffer,
	                     vkb::rendering::RenderTargetCpp &render_target) override;

  private:
	void create_render_pass();
	void create_pipeline();

	std::unique_ptr<vkb::core::BufferCpp> vertex_buffer;

	// Cached pipeline objects — owned by Device's HPPResourceCache.
	vkb::core::HPPRenderPass       *render_pass     = nullptr;
	vkb::core::HPPPipelineLayout   *pipeline_layout = nullptr;
	vkb::core::HPPGraphicsPipeline *pipeline        = nullptr;
};

std::unique_ptr<vkb::Application> create_ohos_triangle();
