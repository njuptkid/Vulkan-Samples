/* Copyright (c) 2025, Contributors
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

/*
 * Image Post Processing Sample
 *
 * This sample demonstrates multi-pass image post-processing effects:
 * - Grayscale conversion
 * - Gradient computation (Sobel edge detection)
 * - Gaussian blur
 * - Vignette effect
 *
 * Each pass reads from the previous pass's output using ping-pong buffering.
 * Users can dynamically enable/disable each pass through the GUI.
 */

#include "image_postprocessing.h"

#include "common/vk_common.h"
#include "filesystem/legacy.h"
#include "gui.h"
#include "rendering/postprocessing_pipeline.h"
#include "rendering/subpasses/forward_subpass.h"
#include "rendering/postprocessing_renderpass.h"
#include "core/sampled_image.h"
#include "scene_graph/components/perspective_camera.h"
#include "stats/stats.h"

ImagePostProcessing::ImagePostProcessing()
{
	// Set the sample name
	set_name("Image Post Processing");
}

ImagePostProcessing::~ImagePostProcessing()
{
	if (has_device())
	{
		get_device().wait_idle();
	}
}

bool ImagePostProcessing::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleC::prepare(options))
	{
		return false;
	}

	// Load default scene for demonstration
	load_scene("scenes/space_module/SpaceModule.gltf");

	// Setup camera
	auto &camera_node = vkb::add_free_camera(get_scene(), "main_camera", get_render_context().get_surface_extent());
	camera            = dynamic_cast<vkb::sg::PerspectiveCamera *>(&camera_node.get_component<vkb::sg::Camera>());

	// Setup scene render pipeline
	vkb::ShaderSource scene_vs{"base.vert.spv"};
	vkb::ShaderSource scene_fs{"base.frag.spv"};
	auto              scene_subpass = std::make_unique<vkb::rendering::subpasses::ForwardSubpassC>(get_render_context(), std::move(scene_vs), std::move(scene_fs), get_scene(), *camera);

	scene_pipeline = std::make_unique<vkb::rendering::RenderPipelineC>();
	scene_pipeline->add_subpass(std::move(scene_subpass));

	// Initialize active pass types
	update_active_pass_types();

	// Setup post-processing pipeline
	setup_postprocessing_pipeline();

	// Enable stats
	get_stats().request_stats({vkb::StatIndex::frame_times});

	// Create GUI
	create_gui(*window, &get_stats());

	return true;
}

void ImagePostProcessing::prepare_render_context()
{
	get_render_context().prepare(1, std::bind(&ImagePostProcessing::create_render_target, this, std::placeholders::_1));
}

std::unique_ptr<vkb::rendering::RenderTargetC> ImagePostProcessing::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	// Important: Copy extent by value, not by reference!
	// After swapchain_image is moved, its internal extent becomes {0, 0, 0}
	auto  extent = swapchain_image.get_extent();
	auto  format = swapchain_image.get_format();

	std::vector<vkb::core::Image> images;

	// Attachment 0: Swapchain - for final output
	images.push_back(std::move(swapchain_image));

	// Attachment 1: Depth - transient, only used in scene pass
	auto depth_format = vkb::get_suitable_depth_format(device.get_gpu().get_handle());
	images.emplace_back(
		device,
		extent,
		depth_format,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY,
		VK_SAMPLE_COUNT_1_BIT);

	// Attachment 2: Scene Color - output of scene, input to first postprocessing pass
	images.emplace_back(
		device,
		extent,
		format,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY,
		VK_SAMPLE_COUNT_1_BIT);

	// Attachments 3-4: Intermediate buffers (TempA, TempB) for ping-pong between passes
	// Used for chaining multiple post-processing effects
	VkImageUsageFlags intermediate_usage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

	// TempA
	images.emplace_back(
		device,
		extent,
		format,
		intermediate_usage,
		VMA_MEMORY_USAGE_GPU_ONLY,
		VK_SAMPLE_COUNT_1_BIT);

	// TempB
	images.emplace_back(
		device,
		extent,
		format,
		intermediate_usage,
		VMA_MEMORY_USAGE_GPU_ONLY,
		VK_SAMPLE_COUNT_1_BIT);

	// Setup load/store operations for scene render pass
	// - Swapchain: don't care (will be written by last postprocessing pass)
	// - Depth: clear and discard (transient)
	// - Color: clear and store (input to postprocessing)
	// - TempA/TempB: don't care (not used in scene pass)
	scene_load_store = {
		{VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE},    // Swapchain
		{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE},    // Depth
		{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE},        // Color
		{VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE}, // TempA
		{VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE}, // TempB
	};

	auto render_target = std::make_unique<vkb::rendering::RenderTargetC>(std::move(images));

	// Set initial layouts for all attachments
	// - Swapchain: Will be written by the last postprocessing pass
	// - Color/Depth/TempA/TempB: Will be cleared/written, so start as UNDEFINED
	// Using UNDEFINED tells Vulkan we don't care about previous contents,
	// which is correct for attachments we clear or fully overwrite.
	render_target->set_layout(Swapchain, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	render_target->set_layout(Color, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(Depth, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(TempA, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(TempB, VK_IMAGE_LAYOUT_UNDEFINED);

	return render_target;
}

void ImagePostProcessing::update_active_pass_types()
{
	active_pass_types.clear();

	if (enabled_passes[PassType::Grayscale])
		active_pass_types.push_back(PassType::Grayscale);
	if (enabled_passes[PassType::Gradient])
		active_pass_types.push_back(PassType::Gradient);
	if (enabled_passes[PassType::Blur])
		active_pass_types.push_back(PassType::Blur);
	if (enabled_passes[PassType::Vignette])
		active_pass_types.push_back(PassType::Vignette);
}

uint32_t ImagePostProcessing::get_input_attachment(size_t pass_index) const
{
	if (pass_index == 0)
	{
		// First pass reads from scene color output
		return Color;
	}
	else
	{
		// Subsequent passes use ping-pong:
		// Pass 1 reads from TempA (written by pass 0)
		// Pass 2 reads from TempB (written by pass 1)
		// Pass 3 reads from TempA (written by pass 2)
		// ...
		return (pass_index % 2 == 1) ? TempA : TempB;
	}
}

uint32_t ImagePostProcessing::get_output_attachment(size_t pass_index, size_t total_passes) const
{
	if (pass_index == total_passes - 1)
	{
		// Last pass outputs directly to swapchain
		return Swapchain;
	}
	else
	{
		// Intermediate passes use ping-pong:
		// Pass 0 writes to TempA (read by pass 1)
		// Pass 1 writes to TempB (read by pass 2)
		// Pass 2 writes to TempA (read by pass 3)
		// ...
		return (pass_index % 2 == 0) ? TempA : TempB;
	}
}

void ImagePostProcessing::setup_postprocessing_pipeline()
{
	postprocessing_pipeline = std::make_unique<vkb::PostProcessingPipeline>(
		get_render_context(),
		vkb::ShaderSource{"postprocessing/postprocessing.vert.spv"});

	// Add passes in order based on active_pass_types
	for (auto pass_type : active_pass_types)
	{
		auto &pass = postprocessing_pipeline->add_pass();

		switch (pass_type)
		{
			case PassType::Grayscale:
				pass.set_debug_name("Grayscale");
				pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/grayscale.frag.spv"});
				break;

			case PassType::Gradient:
				pass.set_debug_name("Gradient (Sobel)");
				pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/gradient.frag.spv"});
				break;

			case PassType::Blur:
				pass.set_debug_name("Blur");
				pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/blur.frag.spv"});
				break;

			case PassType::Vignette:
				pass.set_debug_name("Vignette");
				pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/vignette.frag.spv"});
				break;

			default:
				break;
		}
	}

	// If no passes are enabled, add a passthrough to copy color to swapchain
	if (postprocessing_pipeline->get_passes().empty())
	{
		auto &pass = postprocessing_pipeline->add_pass();
		pass.set_debug_name("Passthrough");
		pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/passthrough.frag.spv"});
	}
}

void ImagePostProcessing::draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target)
{
	// Check if we need to rebuild the pipeline
	bool needs_rebuild = false;
	for (size_t i = 0; i < PassType::Count; ++i)
	{
		if (enabled_passes[i] != last_enabled_passes[i])
		{
			needs_rebuild = true;
			break;
		}
	}

	if (needs_rebuild)
	{
		get_device().wait_idle();
		postprocessing_pipeline.reset();
		update_active_pass_types();
		setup_postprocessing_pipeline();
		last_enabled_passes = enabled_passes;
	}

	auto &extent = render_target.get_extent();

	// Set viewport
	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	// Set scissor
	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	// ========================================================================
	// Ensure proper initial layouts for all attachments
	// ========================================================================
	// The render target is reused across frames. After the previous frame,
	// attachments may be in different layouts. We need to ensure they start
	// in the expected layout for this frame's render passes.
	//
	// - Swapchain: Should be in COLOR_ATTACHMENT_OPTIMAL (written by last PP pass)
	// - Color: Should start as UNDEFINED (we clear it in scene pass)
	// - Depth: Should start as UNDEFINED (we clear it in scene pass)
	// - TempA/TempB: Should start as UNDEFINED (written by PP passes)
	//
	// Note: Using UNDEFINED for attachments we clear is more correct than
	// pretending they're in a specific layout, as it tells Vulkan we don't
	// care about the previous contents.
	render_target.set_layout(Swapchain, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	render_target.set_layout(Color, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target.set_layout(Depth, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target.set_layout(TempA, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target.set_layout(TempB, VK_IMAGE_LAYOUT_UNDEFINED);

	// ========================================================================
	// Pass 1: Scene rendering to intermediate color attachment
	// ========================================================================
	auto &scene_subpass = scene_pipeline->get_active_subpass();
	scene_subpass->set_output_attachments({Color});
	// Note: Depth attachment is automatically detected by the framework
	// based on the attachment format (depth formats are recognized)
	scene_pipeline->set_load_store(scene_load_store);

	// Draw scene to Color attachment
	scene_pipeline->draw(command_buffer, render_target);

	// End scene render pass
	command_buffer.end_render_pass();

	// Update layout tracking for Color attachment
	// After the scene render pass ends, the Color attachment is in
	// VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL (the render pass's finalLayout).
	// The framework doesn't automatically track this, so we need to update it
	// manually so that the postprocessing passes can correctly transition from
	// this layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
	render_target.set_layout(Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// ========================================================================
	// Pass 2+: Post-processing chain (ping-pong between intermediate attachments)
	// ========================================================================
	auto &passes = postprocessing_pipeline->get_passes();
	size_t total_passes = passes.size();

	// Pre-configure all passes before drawing
	// This sets up input/output attachments for each pass
	for (size_t i = 0; i < total_passes; ++i)
	{
		// Cast from PostProcessingPassBase to PostProcessingRenderPass
		// to access get_subpass() method
		auto *render_pass = dynamic_cast<vkb::PostProcessingRenderPass *>(passes[i].get());
		auto &subpass = render_pass->get_subpass(0);

		// Determine input and output attachments for this pass
		uint32_t input_attach = get_input_attachment(i);
		uint32_t output_attach = get_output_attachment(i, total_passes);

		// Bind the input attachment as the color sampler
		// Explicitly construct SampledImage to ensure proper conversion
		subpass.bind_sampled_image("color_sampler", vkb::core::SampledImage{input_attach});

		// Set the output attachment for this pass
		// This controls which attachment the pass writes to
		subpass.set_output_attachments({output_attach});
	}

	// Let the pipeline manage pass iteration and render pass lifecycle
	// The pipeline will:
	// 1. Update current_pass_index for each pass
	// 2. Call prepare_draw() which handles image layout transitions
	// 3. Keep the last render pass open for GUI drawing
	postprocessing_pipeline->draw(command_buffer, render_target);

	// ========================================================================
	// GUI rendering (on top of final output)
	// ========================================================================
	if (has_gui())
	{
		get_gui().draw(command_buffer);
	}

	command_buffer.end_render_pass();

	// ========================================================================
	// Prepare swapchain for presentation
	// ========================================================================
	// The swapchain image must be in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR layout
	// before vkQueuePresentKHR is called. The last postprocessing pass leaves
	// it in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, so we need to transition it.
	{
		auto &views = render_target.get_views();

		vkb::ImageMemoryBarrier barrier{};
		barrier.old_layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.new_layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dst_access_mask = 0;
		barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dst_stage_mask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		command_buffer.image_memory_barrier(views[Swapchain], barrier);
	}
}

void ImagePostProcessing::draw_gui()
{
	get_gui().show_options_window(
		[this]() {
			ImGui::Text("Post-processing Passes:");
			ImGui::Text("(Chained: output of each pass feeds into the next)");
			ImGui::Separator();

			ImGui::Checkbox("Grayscale", &enabled_passes[PassType::Grayscale]);
			ImGui::Checkbox("Gradient (Sobel)", &enabled_passes[PassType::Gradient]);
			ImGui::Checkbox("Blur", &enabled_passes[PassType::Blur]);
			ImGui::Checkbox("Vignette", &enabled_passes[PassType::Vignette]);

			ImGui::Separator();
			ImGui::Text("Pass order: Grayscale -> Gradient -> Blur -> Vignette");
			ImGui::Text("Each pass reads from previous pass output.");
		},
		7);
}

std::unique_ptr<vkb::VulkanSampleC> create_image_postprocessing()
{
	return std::make_unique<ImagePostProcessing>();
}
