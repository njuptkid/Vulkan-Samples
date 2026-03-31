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
 * - Luminance gradient visualization
 *
 * The sample loads an external texture image and applies post-processing effects to it.
 * Each pass reads from the previous pass's output using ping-pong buffering.
 * Users can dynamically enable/disable each pass through the GUI.
 */

#include "image_postprocessing.h"

#include "common/vk_common.h"
#include "filesystem/legacy.h"
#include "gui.h"
#include "rendering/postprocessing_pipeline.h"
#include "rendering/postprocessing_renderpass.h"
#include "core/sampled_image.h"
#include "scene_graph/components/image.h"
#include "stats/stats.h"

ImagePostProcessing::ImagePostProcessing()
{
	set_name("Image Post Processing");
}

ImagePostProcessing::~ImagePostProcessing()
{
	if (has_device())
	{
		get_device().wait_idle();
		// source_sampler (unique_ptr) auto-destroys
	}
}

bool ImagePostProcessing::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleC::prepare(options))
	{
		return false;
	}

	// Load external source image
	load_source_image();

	// Setup fullscreen pipeline (draws source texture to Color attachment)
	setup_fullscreen_pipeline();

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
	auto  extent = swapchain_image.get_extent();
	auto  format = swapchain_image.get_format();

	std::vector<vkb::core::Image> images;

	// Attachment 0: Swapchain - for final output
	images.push_back(std::move(swapchain_image));

	// Attachment 1: Depth - for future 3D content extension
	auto depth_format = vkb::get_suitable_depth_format(device.get_gpu().get_handle());
	images.emplace_back(
	    device,
	    extent,
	    depth_format,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY,
	    VK_SAMPLE_COUNT_1_BIT);

	// Attachment 2: Color - output of fullscreen image, input to postprocessing
	images.emplace_back(
	    device,
	    extent,
	    format,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY,
	    VK_SAMPLE_COUNT_1_BIT);

	// Attachments 3-4: Intermediate buffers (TempA, TempB) for ping-pong
	VkImageUsageFlags intermediate_usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_SAMPLED_BIT |
	    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

	images.emplace_back(device, extent, format, intermediate_usage, VMA_MEMORY_USAGE_GPU_ONLY, VK_SAMPLE_COUNT_1_BIT);        // TempA
	images.emplace_back(device, extent, format, intermediate_usage, VMA_MEMORY_USAGE_GPU_ONLY, VK_SAMPLE_COUNT_1_BIT);        // TempB

	auto render_target = std::make_unique<vkb::rendering::RenderTargetC>(std::move(images));

	// Set initial layouts
	render_target->set_layout(Swapchain, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(Depth, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(Color, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(TempA, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(TempB, VK_IMAGE_LAYOUT_UNDEFINED);

	return render_target;
}

void ImagePostProcessing::load_source_image()
{
	// Load texture using framework's scene graph image loader
	source_texture = vkb::sg::Image::load("source_texture", "textures/vulkan_logo_full.ktx", vkb::sg::Image::ContentType::Color);
	source_texture->create_vk_image(get_device());

	auto &device = get_device();
	auto &queue  = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);

	// Create command buffer for uploading
	VkCommandBuffer cmd = device.create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	// Create staging buffer and upload
	vkb::core::BufferC stage_buffer = vkb::core::BufferC::create_staging_buffer(device, source_texture->get_data());

	// Setup buffer copy regions for each mip level
	auto                              &mipmaps = source_texture->get_mipmaps();
	std::vector<VkBufferImageCopy>     copy_regions;
	for (size_t i = 0; i < mipmaps.size(); i++)
	{
		VkBufferImageCopy region               = {};
		region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel       = vkb::to_u32(i);
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount     = 1;
		region.imageExtent.width               = source_texture->get_extent().width >> i;
		region.imageExtent.height              = source_texture->get_extent().height >> i;
		region.imageExtent.depth               = 1;
		region.bufferOffset                    = mipmaps[i].offset;
		copy_regions.push_back(region);
	}

	// Transition to transfer dst
	vkb::image_layout_transition(cmd,
	                             source_texture->get_vk_image().get_handle(),
	                             VK_IMAGE_LAYOUT_UNDEFINED,
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Copy buffer to image
	vkCmdCopyBufferToImage(cmd,
	                       stage_buffer.get_handle(),
	                       source_texture->get_vk_image().get_handle(),
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                       static_cast<uint32_t>(copy_regions.size()),
	                       copy_regions.data());

	// Transition to shader read only
	vkb::image_layout_transition(cmd,
	                             source_texture->get_vk_image().get_handle(),
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	device.flush_command_buffer(cmd, queue.get_handle());

	// Create sampler using framework wrapper
	VkSamplerCreateInfo sampler_info{};
	sampler_info.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter        = VK_FILTER_LINEAR;
	sampler_info.minFilter        = VK_FILTER_LINEAR;
	sampler_info.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.mipLodBias       = 0.0f;
	sampler_info.compareOp        = VK_COMPARE_OP_NEVER;
	sampler_info.minLod           = 0.0f;
	sampler_info.maxLod           = static_cast<float>(mipmaps.size());
	sampler_info.maxAnisotropy    = 1.0f;
	sampler_info.anisotropyEnable = VK_FALSE;
	sampler_info.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

	source_sampler = std::make_unique<vkb::core::Sampler>(device, sampler_info);
}

void ImagePostProcessing::setup_fullscreen_pipeline()
{
	// Create a separate PostProcessingPipeline that renders the source texture
	// to the Color attachment. This is NOT part of the postprocessing chain.
	fullscreen_pp_pipeline = std::make_unique<vkb::PostProcessingPipeline>(
	    get_render_context(),
	    vkb::ShaderSource{"postprocessing/postprocessing.vert.spv"});

	auto &pass    = fullscreen_pp_pipeline->add_pass();
	pass.set_debug_name("Source Image");
	auto &subpass = pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/fullscreen.frag.spv"});

	// Output to Color attachment (index 2 in render target)
	subpass.set_output_attachments({Color});

	// Bind the external source texture using SampledImage(ImageView, Sampler*)
	subpass.bind_sampled_image(
	    "source_texture",
	    vkb::core::SampledImage{source_texture->get_vk_image_view(), source_sampler.get()});
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
	if (enabled_passes[PassType::LuminanceGradient])
		active_pass_types.push_back(PassType::LuminanceGradient);
}

uint32_t ImagePostProcessing::get_input_attachment(size_t pass_index) const
{
	if (pass_index == 0)
	{
		return Color;
	}
	else
	{
		return (pass_index % 2 == 1) ? TempA : TempB;
	}
}

uint32_t ImagePostProcessing::get_output_attachment(size_t pass_index, size_t total_passes) const
{
	if (pass_index == total_passes - 1)
	{
		return Swapchain;
	}
	else
	{
		return (pass_index % 2 == 0) ? TempA : TempB;
	}
}

void ImagePostProcessing::setup_postprocessing_pipeline()
{
	postprocessing_pipeline = std::make_unique<vkb::PostProcessingPipeline>(
	    get_render_context(),
	    vkb::ShaderSource{"postprocessing/postprocessing.vert.spv"});

	// Add post-processing passes
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

			case PassType::LuminanceGradient:
				pass.set_debug_name("Luminance Gradient");
				pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/luminance_gradient.frag.spv"});
				break;

			default:
				break;
		}
	}

	// If no post-processing passes are enabled, add a passthrough
	if (active_pass_types.empty())
	{
		auto &pass = postprocessing_pipeline->add_pass();
		pass.set_debug_name("Passthrough");
		pass.add_subpass(vkb::ShaderSource{"image_postprocessing/glsl/passthrough.frag.spv"});
	}
}

void ImagePostProcessing::draw_fullscreen_quad(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target)
{
	// Draw fullscreen quad using the separate PP pipeline.
	// This pipeline renders the source texture to the Color attachment.
	fullscreen_pp_pipeline->draw(command_buffer, render_target);
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

	// Update layout tracking
	attachment_layouts[Color]     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment_layouts[Depth]     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachment_layouts[Swapchain] = VK_IMAGE_LAYOUT_UNDEFINED;

	for (size_t i = 0; i < AttachmentCount; ++i)
	{
		render_target.set_layout(static_cast<uint32_t>(i), attachment_layouts[i]);
	}

	// Transition Color and Depth attachments before rendering
	{
		auto &views = render_target.get_views();

		vkb::ImageMemoryBarrier color_barrier{};
		color_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		color_barrier.new_layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_barrier.src_access_mask = 0;
		color_barrier.dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		color_barrier.src_stage_mask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		color_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		command_buffer.image_memory_barrier(views[Color], color_barrier);

		vkb::ImageMemoryBarrier depth_barrier{};
		depth_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_barrier.new_layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_barrier.src_access_mask = 0;
		depth_barrier.dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depth_barrier.src_stage_mask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		depth_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		command_buffer.image_memory_barrier(views[Depth], depth_barrier);
	}

	// Draw source texture to Color attachment
	draw_fullscreen_quad(command_buffer, render_target);
	// Close the render pass left open by fullscreen_pp_pipeline (last pass of that pipeline)
	command_buffer.end_render_pass();

	// Get all passes from the pipeline
	auto &passes       = postprocessing_pipeline->get_passes();
	size_t total_passes = passes.size();

	// Configure input/output attachments for each post-processing pass
	// Source texture has been rendered to Color by fullscreen_pp_pipeline
	for (size_t i = 0; i < total_passes; ++i)
	{
		auto *render_pass = dynamic_cast<vkb::PostProcessingRenderPass *>(passes[i].get());
		auto &subpass     = render_pass->get_subpass(0);

		uint32_t input_attach  = get_input_attachment(i);
		uint32_t output_attach = get_output_attachment(i, total_passes);

		subpass.bind_sampled_image("color_sampler", vkb::core::SampledImage{input_attach});
		subpass.set_output_attachments({output_attach});
	}

	// Draw all passes
	postprocessing_pipeline->draw(command_buffer, render_target);

	// Update layout tracking after post-processing
	attachment_layouts[Swapchain] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment_layouts[Color]     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// GUI
	if (has_gui())
	{
		get_gui().draw(command_buffer);
	}

	command_buffer.end_render_pass();

	// Present barrier
	{
		auto &views = render_target.get_views();

		VkImageLayout swapchain_old_layout = attachment_layouts[Swapchain];

		vkb::ImageMemoryBarrier barrier{};
		barrier.old_layout = swapchain_old_layout;
		barrier.new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		if (swapchain_old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else
		{
			barrier.src_access_mask = 0;
			barrier.src_stage_mask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		}
		barrier.dst_access_mask = 0;
		barrier.dst_stage_mask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		command_buffer.image_memory_barrier(views[Swapchain], barrier);
	}

	// Reset layouts for next frame
	attachment_layouts[Swapchain] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachment_layouts[Color]     = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_layouts[Depth]     = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_layouts[TempA]     = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_layouts[TempB]     = VK_IMAGE_LAYOUT_UNDEFINED;
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
		    ImGui::Checkbox("Luminance Gradient (X,Y)", &enabled_passes[PassType::LuminanceGradient]);

		    ImGui::Separator();
		    ImGui::Text("Pass order: Grayscale -> Gradient -> Blur -> Vignette -> LuminanceGradient");
		    ImGui::Text("Source: External texture (vulkan_logo_full.ktx)");
	    },
	    7);
}

std::unique_ptr<vkb::VulkanSampleC> create_image_postprocessing()
{
	return std::make_unique<ImagePostProcessing>();
}
