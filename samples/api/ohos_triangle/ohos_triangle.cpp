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

#include "ohos_triangle.h"

#include "common/hpp_vk_common.h"
#include "core/util/logging.hpp"
#include "filesystem/legacy.h"
#include "rendering/hpp_pipeline_state.h"

#if defined(OHOS)
#include <hilog/log.h>
#define OHOS_LOG_TAG "OHOSTri"
#define OHOS_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, OHOS_LOG_TAG, __VA_ARGS__)
#else
#define OHOS_LOGI(...) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Render pass & pipeline creation
// ---------------------------------------------------------------------------

void OHOSTriangle::create_render_pass()
{
	auto &swapchain = get_render_context().get_swapchain();
	auto  format    = swapchain.get_format();

	vkb::rendering::AttachmentCpp color_attachment{};
	color_attachment.format  = format;
	color_attachment.samples = vk::SampleCountFlagBits::e1;
	color_attachment.usage   = vk::ImageUsageFlagBits::eColorAttachment;

	std::vector<vkb::rendering::AttachmentCpp> attachments = {color_attachment};
	std::vector<vkb::common::HPPLoadStoreInfo>  load_store  = {{vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore}};

	vkb::core::HPPSubpassInfo subpass_info{};
	subpass_info.output_attachments = {0};

	render_pass = &get_device().get_resource_cache().request_render_pass(
	    attachments, load_store, {subpass_info});
}

void OHOSTriangle::create_pipeline()
{
	auto &cache = get_device().get_resource_cache();

	vkb::core::HPPShaderVariant empty_variant{};
	vkb::core::HPPShaderSource  vert_source("ohos_triangle/glsl/triangle.vert.spv");
	vkb::core::HPPShaderSource  frag_source("ohos_triangle/glsl/triangle.frag.spv");

	auto *vert_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_source, empty_variant);
	auto *frag_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_source, empty_variant);

	pipeline_layout = &cache.request_pipeline_layout({vert_shader, frag_shader});

	vkb::rendering::HPPPipelineState pipeline_state{};

	vkb::rendering::HPPVertexInputState vertex_input{};
	vertex_input.bindings   = {{0, sizeof(Vertex), vk::VertexInputRate::eVertex}};
	vertex_input.attributes = {
	    {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)},
	    {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)},
	};
	pipeline_state.set_vertex_input_state(vertex_input);
	pipeline_state.set_input_assembly_state({vk::PrimitiveTopology::eTriangleList, VK_FALSE});

	vkb::rendering::HPPRasterizationState raster{};
	raster.cull_mode  = vk::CullModeFlagBits::eNone;
	raster.front_face = vk::FrontFace::eClockwise;
	pipeline_state.set_rasterization_state(raster);

	pipeline_state.set_viewport_state({1, 1});
	pipeline_state.set_multisample_state({vk::SampleCountFlagBits::e1});
	pipeline_state.set_depth_stencil_state({false, false, vk::CompareOp::eAlways});

	vkb::rendering::HPPColorBlendState blend{};
	vkb::rendering::HPPColorBlendAttachmentState blend_attachment{};
	blend.attachments = {blend_attachment};
	pipeline_state.set_color_blend_state(blend);

	pipeline_state.set_pipeline_layout(*pipeline_layout);
	pipeline_state.set_render_pass(*render_pass);

	pipeline = &cache.request_graphics_pipeline(pipeline_state);
}

// ---------------------------------------------------------------------------
// VulkanSample overrides
// ---------------------------------------------------------------------------

void OHOSTriangle::request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const
{
	vkb::VulkanSampleCpp::request_layers(requested_layers);

	auto it = requested_layers.find("VK_LAYER_KHRONOS_validation");
	if (it != requested_layers.end())
	{
		it->second = vkb::RequestMode::Optional;
	}
}

bool OHOSTriangle::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleCpp::prepare(options))
	{
		return false;
	}

	// Vertex buffer — use Cpp Buffer with DeviceCpp directly
	const Vertex vertices[] = {
	    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	    {{0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};

	vertex_buffer = std::make_unique<vkb::core::BufferCpp>(
	    get_device(),
	    static_cast<vk::DeviceSize>(sizeof(vertices)),
	    vk::BufferUsageFlagBits::eVertexBuffer,
	    VMA_MEMORY_USAGE_AUTO,
	    VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	vertex_buffer->update(vertices, sizeof(vertices));

	create_render_pass();
	create_pipeline();

	OHOS_LOGI("OHOSTriangle::prepare() COMPLETE");
	return true;
}

void OHOSTriangle::draw_renderpass(vkb::core::CommandBufferCpp &command_buffer,
                                    vkb::rendering::RenderTargetCpp &render_target)
{
	// --- Begin render pass ---
	auto &framebuffer = get_device().get_resource_cache().request_framebuffer(render_target, *render_pass);
	auto  vk_cmd      = command_buffer.get_handle();
	auto  extent      = render_target.get_extent();

	vk::ClearValue clear_color{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}};

	vk::RenderPassBeginInfo rp_begin;
	rp_begin.renderPass        = render_pass->get_handle();
	rp_begin.framebuffer       = framebuffer.get_handle();
	rp_begin.renderArea.offset = vk::Offset2D{0, 0};
	rp_begin.renderArea.extent = extent;
	rp_begin.clearValueCount   = 1;
	rp_begin.pClearValues      = &clear_color;
	vk_cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

	// --- Draw triangle ---
	set_viewport_and_scissor(command_buffer, extent);

	vk::Buffer    vb     = vertex_buffer->get_handle();
	vk::DeviceSize offset = 0;
	vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->get_handle());
	vk_cmd.bindVertexBuffers(0, 1, &vb, &offset);
	vk_cmd.draw(3, 1, 0, 0);

	vk_cmd.endRenderPass();
}

// ---------------------------------------------------------------------------
// Factory function — called by napi_init.cpp
// ---------------------------------------------------------------------------

std::unique_ptr<vkb::Application> create_ohos_triangle()
{
	return std::make_unique<OHOSTriangle>();
}
