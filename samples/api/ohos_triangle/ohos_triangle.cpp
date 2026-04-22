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
// Triangle pipeline creation (proven working pattern from original code)
// ---------------------------------------------------------------------------

void OHOSTriangle::create_triangle_pipeline()
{
	auto &cache    = get_device().get_resource_cache();
	auto  format   = get_render_context().get_swapchain().get_format();

	// Render pass: 2 attachments matching the render target (Swapchain + Color)
	// Triangle outputs to Color (index 1), Swapchain (index 0) is unused
	vkb::rendering::AttachmentCpp attachment{};
	attachment.format  = format;
	attachment.samples = vk::SampleCountFlagBits::e1;
	attachment.usage   = vk::ImageUsageFlagBits::eColorAttachment;

	std::vector<vkb::rendering::AttachmentCpp> attachments = {attachment, attachment};
	std::vector<vkb::common::HPPLoadStoreInfo>  load_store  = {
	    {vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare},        // Swapchain
	    {vk::AttachmentLoadOp::eClear,    vk::AttachmentStoreOp::eStore},           // Color
	};

	vkb::core::HPPSubpassInfo subpass_info{};
	subpass_info.output_attachments = {Color};        // output to attachment 1

	tri_render_pass = &cache.request_render_pass(attachments, load_store, {subpass_info});

	// Pipeline
	vkb::core::HPPShaderSource  vert_source("ohos_triangle/glsl/triangle.vert.spv");
	vkb::core::HPPShaderSource  frag_source("ohos_triangle/glsl/triangle.frag.spv");

	auto *vert_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_source, {});
	auto *frag_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_source, {});

	tri_pipeline_layout = &cache.request_pipeline_layout({vert_shader, frag_shader});

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

	pipeline_state.set_pipeline_layout(*tri_pipeline_layout);
	pipeline_state.set_render_pass(*tri_render_pass);

	tri_pipeline = &cache.request_graphics_pipeline(pipeline_state);
}

// ---------------------------------------------------------------------------
// Blur post-processing setup
// ---------------------------------------------------------------------------

void OHOSTriangle::setup_blur()
{
	blur_pipeline = std::make_unique<vkb::HPPPostProcessingPipeline>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"ohos_triangle/glsl/postprocessing.vert.spv"});

	auto &blur_pass = blur_pipeline->add_pass();
	blur_pass.set_debug_name("Blur");

	auto &blur_subpass = blur_pass.add_subpass(
	    vkb::core::HPPShaderSource{"ohos_triangle/glsl/blur.frag.spv"});

	// Read from Color attachment, output to Swapchain
	blur_subpass.bind_sampled_image("color_sampler", vkb::core::HPPSampledImage{Color});
	blur_subpass.set_output_attachments({Swapchain});
}

// ---------------------------------------------------------------------------
// Custom render target: Swapchain + Color attachment
// ---------------------------------------------------------------------------

std::unique_ptr<vkb::rendering::RenderTargetCpp> OHOSTriangle::create_render_target(vkb::core::HPPImage &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto  extent = swapchain_image.get_extent();
	auto  format = swapchain_image.get_format();

	std::vector<vkb::core::HPPImage> images;

	// Attachment 0: Swapchain
	images.push_back(std::move(swapchain_image));

	// Attachment 1: Color (triangle output, blur input)
	images.emplace_back(
	    device, extent, format,
	    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
	    VMA_MEMORY_USAGE_GPU_ONLY,
	    vk::SampleCountFlagBits::e1);

	auto rt = std::make_unique<vkb::rendering::RenderTargetCpp>(std::move(images));
	rt->set_layout(Swapchain, vk::ImageLayout::eUndefined);
	rt->set_layout(Color, vk::ImageLayout::eUndefined);
	return rt;
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

	create_triangle_pipeline();
	setup_blur();

	OHOS_LOGI("OHOSTriangle::prepare() COMPLETE");
	return true;
}

void OHOSTriangle::prepare_render_context()
{
	get_render_context().prepare(1, [this](vkb::core::HPPImage &&swapchain_image) {
		return create_render_target(std::move(swapchain_image));
	});
}

void OHOSTriangle::draw(vkb::core::CommandBufferCpp &command_buffer,
                          vkb::rendering::RenderTargetCpp &render_target)
{
	auto &views  = render_target.get_views();
	auto  extent = render_target.get_extent();
	auto  vk_cmd = command_buffer.get_handle();

	// === Step 1: Render triangle to Color attachment ===

	// Transition Color to ColorAttachmentOptimal
	{
		vkb::common::HPPImageMemoryBarrier barrier{};
		barrier.old_layout      = vk::ImageLayout::eUndefined;
		barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.src_access_mask = {};
		barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		command_buffer.image_memory_barrier(views[Color], barrier);
		render_target.set_layout(Color, barrier.new_layout);
	}

	// Transition Swapchain to ColorAttachmentOptimal
	{
		vkb::common::HPPImageMemoryBarrier barrier{};
		barrier.old_layout      = vk::ImageLayout::eUndefined;
		barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.src_access_mask = {};
		barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		command_buffer.image_memory_barrier(views[Swapchain], barrier);
		render_target.set_layout(Swapchain, barrier.new_layout);
	}

	// Begin triangle render pass (output to Color attachment)
	auto &tri_fb = get_device().get_resource_cache().request_framebuffer(render_target, *tri_render_pass);

	vk::ClearValue clear_values[2];
	clear_values[0] = {};        // Swapchain: DontCare
	clear_values[1] = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};        // Color: Clear to black

	vk::RenderPassBeginInfo rp_begin;
	rp_begin.renderPass        = tri_render_pass->get_handle();
	rp_begin.framebuffer       = tri_fb.get_handle();
	rp_begin.renderArea.offset = vk::Offset2D{0, 0};
	rp_begin.renderArea.extent = extent;
	rp_begin.clearValueCount   = 2;
	rp_begin.pClearValues      = clear_values;
	vk_cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

	set_viewport_and_scissor(command_buffer, extent);

	vk::Buffer    vb     = vertex_buffer->get_handle();
	vk::DeviceSize offset = 0;
	vk_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, tri_pipeline->get_handle());
	vk_cmd.bindVertexBuffers(0, 1, &vb, &offset);
	vk_cmd.draw(3, 1, 0, 0);
	vk_cmd.endRenderPass();

	// === Step 2: Blur — Color → Swapchain via HPPPostProcessingPipeline ===

	blur_pipeline->draw(command_buffer, render_target);
	command_buffer.end_render_pass();

	// === Step 3: Present barrier ===

	{
		vkb::common::HPPImageMemoryBarrier barrier{};
		barrier.old_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.new_layout      = vk::ImageLayout::ePresentSrcKHR;
		barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
		command_buffer.image_memory_barrier(views[Swapchain], barrier);
		render_target.set_layout(Swapchain, barrier.new_layout);
	}
}

// ---------------------------------------------------------------------------
// Factory function
// ---------------------------------------------------------------------------

std::unique_ptr<vkb::Application> create_ohos_triangle()
{
	return std::make_unique<OHOSTriangle>();
}
