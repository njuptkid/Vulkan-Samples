/* Copyright (c) 2025, Contributors
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

/*
 * Rotation Blur Comparison Sample
 *
 * Two strategies for rotational/radial motion blur:
 *
 *  Method A (CircularCompute): compute shader that, per output pixel, samples N
 *  points along an arc centered on the pixel's polar angle about a rotation
 *  center. Plain linear sampler.
 *
 *  Method B (AnisotropicFrag): fragment shader that performs the same arc
 *  sampling but with a mipmap+anisotropic sampler. The hardware anisotropic
 *  filter widens each tap's footprint along the rotation tangent, so fewer
 *  taps are needed for equivalent coverage.
 *
 * The sample loads an external texture, ensures a mipmap chain exists, and
 * renders the source image to a Color attachment each frame. A post-processing
 * pipeline then applies either method A (compute: Color -> TempA, then a
 * fullscreen copy TempA -> Swapchain) or method B (single fullscreen pass:
 * Color -> Swapchain with anisotropic sampling).
 *
 * Per-frame GPU time is sampled over a sliding window and reported per method.
 */

#include "rotation_blur.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "common/vk_common.h"
#include "core/sampled_image.h"
#include "filesystem/legacy.h"
#include "gui.h"
#include "rendering/postprocessing_computepass.h"
#include "rendering/postprocessing_pipeline.h"
#include "rendering/postprocessing_renderpass.h"
#include "scene_graph/components/image.h"
#include "stats/stats.h"

RotationBlur::RotationBlur()
{
	set_name("Rotation Blur Comparison");

	// Default parameters: rotate around image center, sweep ~20 degrees.
	params.center       = glm::vec2(0.5f, 0.5f);
	params.total_angle  = glm::radians(20.0f);
	params.sample_count = compute_samples;
	params.radius_scale = 1.0f;
}

RotationBlur::~RotationBlur()
{
	if (has_device())
	{
		get_device().wait_idle();
	}
}

bool RotationBlur::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleC::prepare(options))
	{
		return false;
	}

	load_source_image();

	// Samplers used to sample source_texture directly.
	// maxLod now spans the full generated mipmap chain so Method B can actually
	// benefit from hardware mipmap prefiltering (the whole point of the comparison).
	auto &device = get_device();

	VkSamplerCreateInfo linear_info{};
	linear_info.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	linear_info.magFilter         = VK_FILTER_LINEAR;
	linear_info.minFilter         = VK_FILTER_LINEAR;
	linear_info.mipmapMode        = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	linear_info.addressModeU      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	linear_info.addressModeV      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	linear_info.addressModeW      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	linear_info.compareOp         = VK_COMPARE_OP_NEVER;
	linear_info.minLod            = 0.0f;
	linear_info.maxLod            = static_cast<float>(source_texture->get_mipmaps().size());
	linear_info.maxAnisotropy     = 1.0f;
	linear_info.anisotropyEnable  = VK_FALSE;
	linear_info.borderColor       = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	linear_sampler = std::make_unique<vkb::core::Sampler>(device, linear_info);

	VkSamplerCreateInfo aniso_info = linear_info;
	if (device.get_gpu().get_features().samplerAnisotropy)
	{
		aniso_info.maxAnisotropy    = device.get_gpu().get_properties().limits.maxSamplerAnisotropy;
		aniso_info.anisotropyEnable = VK_TRUE;
	}
	aniso_sampler = std::make_unique<vkb::core::Sampler>(device, aniso_info);

	setup_postprocessing_pipeline();

	get_stats().request_stats({vkb::StatIndex::frame_times});
	create_gui(*window, &get_stats());

	return true;
}

void RotationBlur::prepare_render_context()
{
	get_render_context().prepare(1, std::bind(&RotationBlur::create_render_target, this, std::placeholders::_1));
}

std::unique_ptr<vkb::rendering::RenderTargetC> RotationBlur::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto  extent = swapchain_image.get_extent();

	std::vector<vkb::core::Image> images;

	// Attachment 0: Swapchain
	images.push_back(std::move(swapchain_image));

	// Attachment 1: Depth (unused but kept for consistency with the framework)
	auto depth_format = vkb::get_suitable_depth_format(device.get_gpu().get_handle());
	images.emplace_back(
	    device, extent, depth_format,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY, VK_SAMPLE_COUNT_1_BIT);

	// Attachment 2: TempA - compute output (storage image) and copy source.
	// IMPORTANT: must be a LINEAR unorm format. sRGB formats do not support
	// VK_IMAGE_USAGE_STORAGE_IMAGE_BIT, and a storage image declared as
	// `rgba8` in the shader requires an R8G8B8A8_UNORM view. Keeping TempA
	// linear also makes the sRGB encode/decode path symmetric with Method B:
	//   sample source (sRGB->linear) -> imageStore TempA (linear, no encode)
	//   -> sample TempA (linear, no decode) -> write Swapchain (linear->sRGB)
	images.emplace_back(
	    device, extent, VK_FORMAT_R8G8B8A8_UNORM,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
	        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY, VK_SAMPLE_COUNT_1_BIT);

	auto render_target = std::make_unique<vkb::rendering::RenderTargetC>(std::move(images));

	render_target->set_layout(Swapchain, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(Depth, VK_IMAGE_LAYOUT_UNDEFINED);
	render_target->set_layout(TempA, VK_IMAGE_LAYOUT_UNDEFINED);

	return render_target;
}

void RotationBlur::load_source_image()
{
	source_texture = vkb::sg::Image::load("source_texture", "textures/vulkan_logo_full.ktx", vkb::sg::Image::ContentType::Color);
	source_texture->create_vk_image(get_device());

	auto &device = get_device();
	auto &queue  = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);

	// Generate the full mipmap chain on the CPU *before* creating the VkImage.
	// This makes mipmaps.size() == full chain, so create_vk_image() allocates a
	// VkImage with mipLevels = full chain, and the staging upload below copies
	// every level. Without this, Method B (mipmap+anisotropic) has no mip chain
	// to prefilter along the rotation tangent and degrades to base-level-only
	// anisotropic sampling.
	source_texture->generate_mipmaps();

	source_texture->create_vk_image(get_device());

	VkCommandBuffer cmd = device.create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	vkb::core::BufferC stage_buffer = vkb::core::BufferC::create_staging_buffer(device, source_texture->get_data());

	auto                          &mipmaps = source_texture->get_mipmaps();
	std::vector<VkBufferImageCopy> copy_regions;
	for (size_t i = 0; i < mipmaps.size(); i++)
	{
		VkBufferImageCopy region               = {};
		region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel       = vkb::to_u32(i);
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount     = 1;
		region.imageExtent.width               = mipmaps[i].extent.width;
		region.imageExtent.height              = mipmaps[i].extent.height;
		region.imageExtent.depth               = 1;
		region.bufferOffset                    = mipmaps[i].offset;
		copy_regions.push_back(region);
	}

	vkb::image_layout_transition(cmd,
	                             source_texture->get_vk_image().get_handle(),
	                             VK_IMAGE_LAYOUT_UNDEFINED,
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	vkCmdCopyBufferToImage(cmd,
	                       stage_buffer.get_handle(),
	                       source_texture->get_vk_image().get_handle(),
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                       static_cast<uint32_t>(copy_regions.size()),
	                       copy_regions.data());

	vkb::image_layout_transition(cmd,
	                             source_texture->get_vk_image().get_handle(),
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	device.flush_command_buffer(cmd, queue.get_handle());

	// source_sampler is no longer used for an intermediate draw, but kept for
	// potential diagnostics; the per-method samplers (linear/aniso) are what
	// the shaders use to sample source_texture directly.
	VkSamplerCreateInfo sampler_info{};
	sampler_info.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter         = VK_FILTER_LINEAR;
	sampler_info.minFilter         = VK_FILTER_LINEAR;
	sampler_info.mipmapMode        = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.addressModeU      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.compareOp         = VK_COMPARE_OP_NEVER;
	sampler_info.minLod            = 0.0f;
	sampler_info.maxLod            = static_cast<float>(mipmaps.size());
	sampler_info.maxAnisotropy     = 1.0f;
	sampler_info.anisotropyEnable  = VK_FALSE;
	sampler_info.borderColor       = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	source_sampler = std::make_unique<vkb::core::Sampler>(device, sampler_info);
}

RotationBlur::Params RotationBlur::build_params() const
{
	Params p{};
	p.center       = params.center;
	p.total_angle  = glm::radians(sweep_degrees);
	p.radius_scale = params.radius_scale;
	p.sample_count = (method == Method::CircularCompute) ? compute_samples : aniso_samples;
	return p;
}

void RotationBlur::setup_postprocessing_pipeline()
{
	postprocessing_pipeline = std::make_unique<vkb::PostProcessingPipeline>(
	    get_render_context(),
	    vkb::ShaderSource{"postprocessing/postprocessing.vert.spv"});

	// Both methods sample source_texture directly (full 3624x3624 + mipmap
	// chain). This is what makes Method B meaningful: textureGrad() can pick a
	// higher mip level (prefiltered) along the rotation tangent.
	if (method == Method::CircularCompute)
	{
		// Pass 0: compute circular sampling, source -> TempA.
		auto &compute_pass = postprocessing_pipeline->add_pass<vkb::PostProcessingComputePass>(
		    vkb::ShaderSource{"rotation_blur/glsl/rotation_blur.comp.spv"});
		compute_pass.set_debug_name("Rotation Blur (Compute)");
		compute_pass.bind_sampled_image(
		    "src_image",
		    vkb::core::SampledImage{source_texture->get_vk_image_view(), linear_sampler.get()});
		compute_pass.bind_storage_image("out_image", vkb::core::SampledImage{TempA});
		compute_pass.set_uniform_data(build_params());

		// Pass 1: fullscreen copy TempA -> Swapchain.
		auto &copy_pass = postprocessing_pipeline->add_pass();
		copy_pass.set_debug_name("Copy to Swapchain");
		auto &copy_subpass = copy_pass.add_subpass(
		    vkb::ShaderSource{"rotation_blur/glsl/fullscreen_copy.frag.spv"});
		copy_subpass.bind_sampled_image("color_sampler", vkb::core::SampledImage{TempA});
		copy_subpass.set_output_attachments({Swapchain});
	}
	else
	{
		// Single pass: source -> Swapchain with anisotropic + mipmap sampling.
		auto &pass = postprocessing_pipeline->add_pass();
		pass.set_debug_name("Rotation Blur (Anisotropic)");
		auto &subpass = pass.add_subpass(
		    vkb::ShaderSource{"rotation_blur/glsl/rotation_blur_aniso.frag.spv"});
		subpass.set_output_attachments({Swapchain});
		// color_sampler is bound per-frame in draw() with the anisotropic sampler
		// and the Color attachment view (because the swapchain view can change).
	}
}

void RotationBlur::rebuild_pipeline_if_needed()
{
	// Only a method switch requires rebuilding the pipeline (different passes).
	// Tap counts are uniforms updated each frame via set_uniform_data(), so
	// changing them does NOT require a rebuild.
	if (method != gui_method)
	{
		method = gui_method;
		get_device().wait_idle();
		postprocessing_pipeline.reset();
		setup_postprocessing_pipeline();
	}
}

void RotationBlur::update(float delta_time)
{
	// Sliding-window accumulation per method; report averages to the benchmark.
	rebuild_pipeline_if_needed();

	const float dt_ms = delta_time * 1000.0f;
	const auto  idx  = static_cast<size_t>(method);
	accum_dt_ms[idx]   += dt_ms;
	accum_frames[idx]   += 1;

	window_accum_time += delta_time;
	if (window_accum_time >= window_seconds)
	{
		benchmarks[idx].frame_time_ms = accum_dt_ms[idx] / std::max(1u, accum_frames[idx]);
		benchmarks[idx].frames       += accum_frames[idx];
		// Reset both accumulators so each method's next reading starts fresh.
		accum_dt_ms[0]   = 0.0f;
		accum_dt_ms[1]   = 0.0f;
		accum_frames[0]  = 0;
		accum_frames[1]  = 0;
		window_accum_time = 0.0f;
	}

	VulkanSample::update(delta_time);
}

void RotationBlur::draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target)
{
	// Reset all attachment layouts to UNDEFINED; the framework's
	// transition_attachments() handles the rest automatically per pass.
	for (uint32_t i = 0; i < AttachmentCount; ++i)
	{
		render_target.set_layout(i, VK_IMAGE_LAYOUT_UNDEFINED);
	}

	// Per-frame binding for the active method. Both methods now sample
	// source_texture directly (no intermediate Color attachment draw), so
	// Method B can actually use the source's mipmap chain.
	auto &extent = render_target.get_extent();
	auto  p      = build_params();

	if (method == Method::CircularCompute)
	{
		auto &compute_pass = postprocessing_pipeline->get_pass<vkb::PostProcessingComputePass>(0);
		compute_pass.set_dispatch_size({(extent.width + 7u) / 8u,
		                                (extent.height + 7u) / 8u, 1u});
		compute_pass.set_uniform_data(p);
		compute_pass.bind_sampled_image(
		    "src_image",
		    vkb::core::SampledImage{source_texture->get_vk_image_view(), linear_sampler.get()});
		compute_pass.bind_storage_image("out_image", vkb::core::SampledImage{TempA});

		auto &copy_pass = postprocessing_pipeline->get_pass<vkb::PostProcessingRenderPass>(1);
		copy_pass.get_subpass(0).bind_sampled_image(
		    "color_sampler", vkb::core::SampledImage{TempA});
	}
	else
	{
		auto &pass = postprocessing_pipeline->get_pass<vkb::PostProcessingRenderPass>(0);
		pass.set_uniform_data(p);
		pass.get_subpass(0).bind_sampled_image(
		    "color_sampler",
		    vkb::core::SampledImage{source_texture->get_vk_image_view(), aniso_sampler.get()});
	}

	// Run the post-processing pipeline (Method A: compute+copy; Method B: frag).
	postprocessing_pipeline->draw(command_buffer, render_target);

	// GUI overlay.
	if (has_gui())
	{
		get_gui().draw(command_buffer);
	}
	command_buffer.end_render_pass();

	// Present barrier for the swapchain.
	{
		auto &views = render_target.get_views();

		vkb::ImageMemoryBarrier barrier{};
		barrier.old_layout       = render_target.get_layout(Swapchain);
		barrier.new_layout       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.src_access_mask  = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.src_stage_mask   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dst_access_mask  = 0;
		barrier.dst_stage_mask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		command_buffer.image_memory_barrier(views[Swapchain], barrier);
	}
}

void RotationBlur::draw_gui()
{
	get_gui().show_options_window(
	    [this]() {
		    ImGui::Text("Rotation Blur Method");
		    ImGui::RadioButton("A: Circular Compute",
		                       reinterpret_cast<int *>(&gui_method),
		                       static_cast<int>(Method::CircularCompute));
		    ImGui::SameLine();
		    ImGui::RadioButton("B: Mipmap+Anisotropic",
		                       reinterpret_cast<int *>(&gui_method),
		                       static_cast<int>(Method::AnisotropicFrag));

	    ImGui::Separator();
	    ImGui::SliderFloat("Sweep Angle (deg)", &sweep_degrees, 0.0f, 180.0f, "%.1f");
	    ImGui::SliderFloat2("Center (UV)", &params.center[0], 0.0f, 1.0f, "%.3f");
	    ImGui::SliderFloat("Radius Scale", &params.radius_scale, 0.25f, 4.0f, "%.2f");

	    ImGui::Separator();
	    ImGui::Text("Samples per method (independent):");
	    ImGui::SliderInt("Compute max taps (adaptive A)",
	                     reinterpret_cast<int *>(&compute_samples), 1, 1024);
	    ImGui::SliderInt("Aniso taps (B)", reinterpret_cast<int *>(&aniso_samples), 1, 64);
	    ImGui::Text("(A: N adapts so taps <= 1 src texel apart, capped above)");

		    ImGui::Separator();
		    ImGui::Text("Performance (rolling %d ms window):", static_cast<int>(window_seconds * 1000));
		    const char *names[2] = {"A: Compute", "B: Anisotropic"};
		    for (int i = 0; i < 2; ++i)
		    {
			    float avg = (benchmarks[i].frames > 0) ? benchmarks[i].frame_time_ms : 0.0f;
			    ImGui::Text("%s: %.3f ms/frame  (%u frames sampled)",
			                names[i], avg, benchmarks[i].frames);
		    }

		    ImGui::Separator();
		    auto &se = source_texture->get_extent();
		    ImGui::Text("Source: textures/vulkan_logo_full.ktx");
		    ImGui::Text("Source extent: %ux%u", se.width, se.height);
		    ImGui::Text("Mip levels: %zu", source_texture->get_mipmaps().size());
		    ImGui::Text("Source format: 0x%x",
		                static_cast<uint32_t>(source_texture->get_format()));
		    bool aniso_enabled = (aniso_sampler != nullptr);
		    ImGui::Text("Anisotropic: %s", aniso_enabled ? "enabled" : "unsupported");
	    },
	    7);
}

std::unique_ptr<vkb::VulkanSampleC> create_rotation_blur()
{
	return std::make_unique<RotationBlur>();
}
