/* Copyright (c) 2024, Huawei Technologies Co., Ltd.
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

#pragma once

#include "vulkan_sample.h"

#include "core/buffer.h"
#include "core/hpp_pipeline.h"
#include "core/hpp_pipeline_layout.h"
#include "core/hpp_render_pass.h"
#include "core/hpp_shader_module.h"
#include "rendering/hpp_postprocessing_pipeline.h"
#include "rendering/hpp_postprocessing_renderpass.h"
#include "rendering/render_target.h"
#include <vk_mem_alloc.h>

class OHOSTriangle : public vkb::VulkanSampleCpp
{
	struct Vertex
	{
		float pos[3];
		float color[3];
	};

	enum AttachmentIndex
	{
		Swapchain = 0,
		Color     = 1,
		AttachmentCount
	};

  public:
	OHOSTriangle()          = default;
	~OHOSTriangle() override = default;

	bool prepare(const vkb::ApplicationOptions &options) override;

	void request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const override;

	// Override draw() to manage full frame: triangle → blur
	void draw(vkb::core::CommandBufferCpp &command_buffer,
	          vkb::rendering::RenderTargetCpp &render_target) override;

  protected:
	void prepare_render_context() override;

  private:
	void create_triangle_pipeline();
	void setup_blur();

	std::unique_ptr<vkb::rendering::RenderTargetCpp>
	    create_render_target(vkb::core::HPPImage &&swapchain_image);

	// Triangle (raw vk pipeline)
	vkb::core::HPPRenderPass       *tri_render_pass   = nullptr;
	vkb::core::HPPPipelineLayout   *tri_pipeline_layout = nullptr;
	vkb::core::HPPGraphicsPipeline *tri_pipeline      = nullptr;
	std::unique_ptr<vkb::core::BufferCpp> vertex_buffer;

	// Blur (postprocessing pipeline)
	std::unique_ptr<vkb::HPPPostProcessingPipeline> blur_pipeline;
};

std::unique_ptr<vkb::Application> create_ohos_triangle();
