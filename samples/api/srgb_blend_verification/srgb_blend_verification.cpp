/* Copyright (c) 2026, Arm Limited and Contributors
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

#include "srgb_blend_verification.h"

#include "common/vk_common.h"
#include "core/buffer.h"
#include "core/util/logging.hpp"
#include "gui.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace
{
// sRGB transfer functions (canonical, IEC 61966-2-1)
float srgb_to_linear_f(float s)
{
	if (s <= 0.04045f)
	{
		return s / 12.92f;
	}
	return std::pow((s + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb_f(float l)
{
	if (l <= 0.0031308f)
	{
		return 12.92f * l;
	}
	return 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

uint8_t float_to_u8(float v)
{
	// Vulkan hardware typically uses round-to-nearest for the 8-bit quantization
	// of the sRGB-encoded blend result.
	int32_t i = static_cast<int32_t>(std::lround(v * 255.0f));
	if (i < 0)
		i = 0;
	if (i > 255)
		i = 255;
	return static_cast<uint8_t>(i);
}
}    // namespace

// ============================================================================
// Lifecycle
// ============================================================================

SrgbBlendVerification::SrgbBlendVerification()
{
	title = "sRGB Hardware Blend Verification";

	// Dynamic rendering: no VkRenderPass/VkFramebuffer.
	add_device_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
	add_device_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

	render_pass = VK_NULL_HANDLE;
}

SrgbBlendVerification::~SrgbBlendVerification()
{
	if (has_device())
	{
		// Offscreen rendering happens once in prepare(); images may already be in
		// TRANSFER_SRC_OPTIMAL layout. get_device().wait_idle() is called by the
		// base class destructor chain, so it's safe to destroy here.
		destroy_offscreen_image(srgb_image_);
		destroy_offscreen_image(unorm_image_);
		destroy_offscreen_image(display_image_);

		vkDestroyPipeline(get_device().get_handle(), srgb_pipeline_, nullptr);
		vkDestroyPipeline(get_device().get_handle(), unorm_pipeline_, nullptr);
		vkDestroyPipelineLayout(get_device().get_handle(), pipeline_layout_, nullptr);

		srgb_readback_buffer_.reset();
		unorm_readback_buffer_.reset();
	}
}

void SrgbBlendVerification::request_gpu_features(vkb::core::PhysicalDeviceC &gpu)
{
	REQUEST_REQUIRED_FEATURE(gpu, VkPhysicalDeviceDynamicRenderingFeaturesKHR, dynamicRendering);
	REQUEST_REQUIRED_FEATURE(gpu, VkPhysicalDeviceSynchronization2FeaturesKHR, synchronization2);
}

// ============================================================================
// Prepare
// ============================================================================

bool SrgbBlendVerification::prepare(const vkb::ApplicationOptions &options)
{
	if (!ApiVulkanSample::prepare(options))
	{
		return false;
	}

	// Add TRANSFER_DST so we can blit our offscreen images into the swapchain.
	update_swapchain_image_usage_flags({VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT});

	// Patch table: bg sRGB byte, its linear value, and predicted blend bytes
	// for the two hypotheses.
	//
	//   expected_srgb_linear:
	//     What the sRGB attachment would store if the hardware does linearize
	//     the destination, blend in linear space, and re-encode to sRGB.
	//   expected_srgb_nonlinear:
	//     What the sRGB attachment would store if the hardware did NOT linearize
	//     the destination (treating the sRGB-encoded byte as a raw normalized
	//     value and blending directly in sRGB space).
	//   expected_unorm:
	//     What the UNORM control attachment stores. No sRGB path is active here
	//     at all, so the framebuffer byte is the linearized bg value stored as
	//     a raw byte, and the blend happens on that raw byte.
	//
	// Patch 1 (black bg) is the strongest discriminator: 188 vs 128.
	patches_ = {{
	    {"P1", 0,
	     srgb_to_linear_f(0.0f / 255.0f),
	     float_to_u8(linear_to_srgb_f(0.5f + 0.5f * srgb_to_linear_f(0.0f / 255.0f))),
	     float_to_u8(0.5f + 0.5f * (0.0f / 255.0f)),
	     float_to_u8(0.5f + 0.5f * srgb_to_linear_f(0.0f / 255.0f))},
	    {"P2", 51,
	     srgb_to_linear_f(51.0f / 255.0f),
	     float_to_u8(linear_to_srgb_f(0.5f + 0.5f * srgb_to_linear_f(51.0f / 255.0f))),
	     float_to_u8(0.5f + 0.5f * (51.0f / 255.0f)),
	     float_to_u8(0.5f + 0.5f * srgb_to_linear_f(51.0f / 255.0f))},
	    {"P3", 128,
	     srgb_to_linear_f(128.0f / 255.0f),
	     float_to_u8(linear_to_srgb_f(0.5f + 0.5f * srgb_to_linear_f(128.0f / 255.0f))),
	     float_to_u8(0.5f + 0.5f * (128.0f / 255.0f)),
	     float_to_u8(0.5f + 0.5f * srgb_to_linear_f(128.0f / 255.0f))},
	}};

	create_offscreen_image(srgb_image_, VK_FORMAT_R8G8B8A8_SRGB,
	                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	create_offscreen_image(unorm_image_, VK_FORMAT_R8G8B8A8_UNORM,
	                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	create_offscreen_image(display_image_, VK_FORMAT_R8G8B8A8_SRGB,
	                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

	create_pipelines();

	const VkDeviceSize buf_size = kImageWidth * kImageHeight * 4ull;
	srgb_readback_buffer_  = std::make_unique<vkb::core::BufferC>(get_device(), buf_size,
	                                                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                              VMA_MEMORY_USAGE_GPU_TO_CPU,
	                                                              VMA_ALLOCATION_CREATE_MAPPED_BIT);
	unorm_readback_buffer_ = std::make_unique<vkb::core::BufferC>(get_device(), buf_size,
	                                                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                              VMA_MEMORY_USAGE_GPU_TO_CPU,
	                                                              VMA_ALLOCATION_CREATE_MAPPED_BIT);

	// One-time offscreen rendering, UNORM->display copy, and readback.
	VkCommandBuffer cmd = get_device().create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	render_offscreen_once(cmd);
	get_device().flush_command_buffer(cmd, queue);

	readback_and_verify();

	// Offscreen images are now in TRANSFER_SRC_OPTIMAL layout (srgb_image_ and
	// display_image_), ready for per-frame blits to the swapchain.
	build_command_buffers();

	prepared = true;
	return true;
}

// ============================================================================
// Resource creation
// ============================================================================

void SrgbBlendVerification::create_offscreen_image(OffscreenImage &img, VkFormat format, VkImageUsageFlags usage)
{
	VkImageCreateInfo ici{};
	ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType     = VK_IMAGE_TYPE_2D;
	ici.format        = format;
	ici.extent        = {kImageWidth, kImageHeight, 1};
	ici.mipLevels     = 1;
	ici.arrayLayers   = 1;
	ici.samples       = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
	ici.usage         = usage;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK(vkCreateImage(get_device().get_handle(), &ici, nullptr, &img.image));
	img.format = format;

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(get_device().get_handle(), img.image, &mem_reqs);

	VkMemoryAllocateInfo mai{};
	mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize  = mem_reqs.size;
	mai.memoryTypeIndex = get_device().get_gpu().get_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(get_device().get_handle(), &mai, nullptr, &img.memory));
	VK_CHECK(vkBindImageMemory(get_device().get_handle(), img.image, img.memory, 0));

	VkImageViewCreateInfo vci{};
	vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
	vci.image                           = img.image;
	vci.format                          = format;
	vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	vci.subresourceRange.baseMipLevel   = 0;
	vci.subresourceRange.levelCount     = 1;
	vci.subresourceRange.baseArrayLayer = 0;
	vci.subresourceRange.layerCount     = 1;
	VK_CHECK(vkCreateImageView(get_device().get_handle(), &vci, nullptr, &img.view));
}

void SrgbBlendVerification::destroy_offscreen_image(OffscreenImage &img)
{
	if (img.view)
	{
		vkDestroyImageView(get_device().get_handle(), img.view, nullptr);
		img.view = VK_NULL_HANDLE;
	}
	if (img.image)
	{
		vkDestroyImage(get_device().get_handle(), img.image, nullptr);
		img.image = VK_NULL_HANDLE;
	}
	if (img.memory)
	{
		vkFreeMemory(get_device().get_handle(), img.memory, nullptr);
		img.memory = VK_NULL_HANDLE;
	}
	img.format = VK_FORMAT_UNDEFINED;
}

void SrgbBlendVerification::create_pipeline(VkFormat color_format, VkPipeline &out_pipeline)
{
	VkPipelineShaderStageCreateInfo shader_stages[2] = {
	    load_shader("srgb_blend_verification", "quad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
	    load_shader("srgb_blend_verification", "quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	VkPipelineLayoutCreateInfo plci = vkb::initializers::pipeline_layout_create_info(nullptr, 0);
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.offset     = 0;
	pcr.size       = sizeof(PushConstants);
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges    = &pcr;
	VK_CHECK(vkCreatePipelineLayout(get_device().get_handle(), &plci, nullptr, &pipeline_layout_));

	VkPipelineInputAssemblyStateCreateInfo iaci = vkb::initializers::pipeline_input_assembly_state_create_info(
	    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rsci = vkb::initializers::pipeline_rasterization_state_create_info(
	    VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

	VkPipelineColorBlendAttachmentState cbas = vkb::initializers::pipeline_color_blend_attachment_state(
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	    VK_TRUE);
	cbas.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cbas.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cbas.colorBlendOp        = VK_BLEND_OP_ADD;
	cbas.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cbas.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	cbas.alphaBlendOp        = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo cbsci = vkb::initializers::pipeline_color_blend_state_create_info(1, &cbas);

	VkPipelineDepthStencilStateCreateInfo dssci{};
	dssci.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dssci.depthTestEnable  = VK_FALSE;
	dssci.depthWriteEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo vpsci = vkb::initializers::pipeline_viewport_state_create_info(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo mssci = vkb::initializers::pipeline_multisample_state_create_info(
	    VK_SAMPLE_COUNT_1_BIT, 0);

	std::vector<VkDynamicState> dyn_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo pdsci = vkb::initializers::pipeline_dynamic_state_create_info(
	    dyn_states.data(), static_cast<uint32_t>(dyn_states.size()), 0);

	VkPipelineVertexInputStateCreateInfo visci = vkb::initializers::pipeline_vertex_input_state_create_info();

	VkPipelineRenderingCreateInfoKHR prci{};
	prci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	prci.colorAttachmentCount    = 1;
	prci.pColorAttachmentFormats = &color_format;

	VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	gpci.pNext               = &prci;
	gpci.layout             = pipeline_layout_;
	gpci.stageCount          = 2;
	gpci.pStages             = shader_stages;
	gpci.pInputAssemblyState = &iaci;
	gpci.pRasterizationState = &rsci;
	gpci.pColorBlendState    = &cbsci;
	gpci.pDepthStencilState = &dssci;
	gpci.pViewportState      = &vpsci;
	gpci.pMultisampleState   = &mssci;
	gpci.pDynamicState       = &pdsci;
	gpci.pVertexInputState   = &visci;
	gpci.renderPass          = VK_NULL_HANDLE;    // dynamic rendering
	gpci.subpass             = 0;

	VK_CHECK(vkCreateGraphicsPipelines(get_device().get_handle(), pipeline_cache, 1, &gpci, nullptr, &out_pipeline));
}

void SrgbBlendVerification::create_pipelines()
{
	create_pipeline(VK_FORMAT_R8G8B8A8_SRGB, srgb_pipeline_);
	create_pipeline(VK_FORMAT_R8G8B8A8_UNORM, unorm_pipeline_);
}

// ============================================================================
// Offscreen rendering (one-time, in prepare)
// ============================================================================

void SrgbBlendVerification::draw_patches(VkCommandBuffer cmd, VkPipeline pipeline)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	const VkViewport vp = vkb::initializers::viewport(
	    static_cast<float>(kImageWidth), static_cast<float>(kImageHeight), 0.0f, 1.0f);
	vkCmdSetViewport(cmd, 0, 1, &vp);

	// Full-image scissor; per-patch scissor is set per draw.
	VkRect2D scissor = vkb::initializers::rect2D(kImageWidth, kImageHeight, 0, 0);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	for (uint32_t p = 0; p < kPatchCount; ++p)
	{
		const auto &patch = patches_[p];

		// Per-patch scissor (limits the fullscreen triangle to this patch region)
		VkRect2D patch_scissor = vkb::initializers::rect2D(
		    kPatchSize, kImageHeight, p * kPatchSize, 0);
		vkCmdSetScissor(cmd, 0, 1, &patch_scissor);

		// 1) Background fill: alpha=1.0 overwrites the cleared value with the
		//    linearized bg color. On an sRGB attachment this stores bg_srgb_byte;
		//    on a UNORM attachment this stores (bg_srgb_byte/255) (raw, no sRGB).
		PushConstants bg_pc{patch.bg_linear, patch.bg_linear, patch.bg_linear, 1.0f};
		vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, sizeof(PushConstants), &bg_pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		// 2) Blend test: white * 0.5 + dst * 0.5.
		PushConstants fg_pc{1.0f, 1.0f, 1.0f, 0.5f};
		vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, sizeof(PushConstants), &fg_pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
}

void SrgbBlendVerification::render_offscreen_once(VkCommandBuffer cmd)
{
	const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	// --- 1. Render to sRGB image ---
	{
		VkRenderingAttachmentInfoKHR att{};
		att.sType         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		att.imageView     = srgb_image_.view;
		att.imageLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		att.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		att.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		att.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

		VkRenderingInfoKHR ri{};
		ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
		ri.renderArea.extent    = {kImageWidth, kImageHeight};
		ri.layerCount           = 1;
		ri.colorAttachmentCount = 1;
		ri.pColorAttachments    = &att;

		vkb::image_layout_transition(cmd, srgb_image_.image,
		                             VK_IMAGE_LAYOUT_UNDEFINED,
		                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                             color_range);

		vkCmdBeginRenderingKHR(cmd, &ri);
		draw_patches(cmd, srgb_pipeline_);
		vkCmdEndRenderingKHR(cmd);
	}

	// --- 2. Render to UNORM image (control) ---
	{
		VkRenderingAttachmentInfoKHR att{};
		att.sType         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		att.imageView     = unorm_image_.view;
		att.imageLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		att.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
		att.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
		att.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

		VkRenderingInfoKHR ri{};
		ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
		ri.renderArea.extent    = {kImageWidth, kImageHeight};
		ri.layerCount           = 1;
		ri.colorAttachmentCount = 1;
		ri.pColorAttachments    = &att;

		vkb::image_layout_transition(cmd, unorm_image_.image,
		                             VK_IMAGE_LAYOUT_UNDEFINED,
		                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                             color_range);

		vkCmdBeginRenderingKHR(cmd, &ri);
		draw_patches(cmd, unorm_pipeline_);
		vkCmdEndRenderingKHR(cmd);
	}

	// --- 3. Copy UNORM image bytes into the sRGB display image (raw byte copy) ---
	vkb::image_layout_transition(cmd, unorm_image_.image,
	                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                             color_range);
	vkb::image_layout_transition(cmd, display_image_.image,
	                             VK_IMAGE_LAYOUT_UNDEFINED,
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                             color_range);

	VkImageCopy copy_region{};
	copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.srcSubresource.layerCount = 1;
	copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.dstSubresource.layerCount = 1;
	copy_region.extent                    = {kImageWidth, kImageHeight, 1};
	vkCmdCopyImage(cmd,
	               unorm_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	               display_image_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	               1, &copy_region);

	// --- 4. Read back both offscreen images into host buffers ---
	vkb::image_layout_transition(cmd, srgb_image_.image,
	                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                             color_range);

	VkBufferImageCopy bic{};
	bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bic.imageSubresource.layerCount = 1;
	bic.imageExtent                  = {kImageWidth, kImageHeight, 1};
	bic.bufferRowLength              = kImageWidth;
	bic.bufferImageHeight            = kImageHeight;

	vkCmdCopyImageToBuffer(cmd, srgb_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       srgb_readback_buffer_->get_handle(), 1, &bic);
	vkCmdCopyImageToBuffer(cmd, unorm_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       unorm_readback_buffer_->get_handle(), 1, &bic);

	VkBufferMemoryBarrier2KHR bmb[2]{};
	for (int i = 0; i < 2; ++i)
	{
		bmb[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
		bmb[i].srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
		bmb[i].dstStageMask        = VK_PIPELINE_STAGE_2_HOST_BIT_KHR;
		bmb[i].srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
		bmb[i].dstAccessMask       = VK_ACCESS_2_HOST_READ_BIT_KHR;
		bmb[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bmb[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bmb[i].offset              = 0;
		bmb[i].size                = VK_WHOLE_SIZE;
	}
	bmb[0].buffer = srgb_readback_buffer_->get_handle();
	bmb[1].buffer = unorm_readback_buffer_->get_handle();

	VkDependencyInfoKHR dep{};
	dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
	dep.bufferMemoryBarrierCount = 2;
	dep.pBufferMemoryBarriers    = bmb;
	vkCmdPipelineBarrier2KHR(cmd, &dep);

	// --- 5. Transition the display image to TRANSFER_SRC for per-frame blits ---
	vkb::image_layout_transition(cmd, display_image_.image,
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                             color_range);
}

// ============================================================================
// Readback and verdict
// ============================================================================

void SrgbBlendVerification::readback_and_verify()
{
	const uint8_t *srgb_data  = static_cast<const uint8_t *>(srgb_readback_buffer_->map());
	const uint8_t *unorm_data = static_cast<const uint8_t *>(unorm_readback_buffer_->map());

	for (uint32_t p = 0; p < kPatchCount; ++p)
	{
		// Sample at patch center (avoid any boundary interpolation effects).
		const uint32_t x = p * kPatchSize + kPatchSize / 2;
		const uint32_t y = kImageHeight / 2;
		const uint32_t idx = (y * kImageWidth + x) * 4;

		results_[p].srgb_byte  = srgb_data[idx];     // R channel
		results_[p].unorm_byte = unorm_data[idx];    // R channel
	}

	srgb_readback_buffer_->unmap();
	unorm_readback_buffer_->unmap();

	// Verdict: which hypothesis does the sRGB attachment result match?
	// Allow +/-1 tolerance for vendor rounding differences.
	auto classify = [](uint8_t actual, uint8_t lin_exp, uint8_t nonlin_exp) -> const char *
	{
		int d_lin    = std::abs(static_cast<int>(actual) - static_cast<int>(lin_exp));
		int d_nonlin = std::abs(static_cast<int>(actual) - static_cast<int>(nonlin_exp));
		if (d_lin <= 1 && d_nonlin <= 1)
			return "AMBIGUOUS";
		if (d_lin <= 1)
			return "LINEAR";
		if (d_nonlin <= 1)
			return "NON-LINEAR";
		if (d_lin < d_nonlin)
			return "near LINEAR";
		if (d_nonlin < d_lin)
			return "near NON-LINEAR";
		return "MISMATCH";
	};

	for (uint32_t p = 0; p < kPatchCount; ++p)
	{
		verdicts_[p] = classify(results_[p].srgb_byte,
		                         patches_[p].expected_srgb_linear,
		                         patches_[p].expected_srgb_nonlinear);
	}

	verified_ = true;

	// Print the table to the console (via spdlog so it's captured regardless of
	// subsystem) and also write it to a file next to the executable for offline
	// inspection of headless runs.
	LOGI("=== sRGB Hardware Blend Verification ===");
	LOGI("Patch  bg   sRGB-act  lin-exp  nonlin-exp  UNORM-act  unorm-exp  verdict");
	std::string file_text = "=== sRGB Hardware Blend Verification ===\n"
	                       "Patch  bg   sRGB-act  lin-exp  nonlin-exp  UNORM-act  unorm-exp  verdict\n";
	for (uint32_t p = 0; p < kPatchCount; ++p)
	{
		const auto &pi = patches_[p];
		const auto &ri = results_[p];
		char line[200];
		std::snprintf(line, sizeof(line),
		              "  %s  %3u   %3u       %3u      %3u         %3u       %3u       %s",
		              pi.name, pi.bg_srgb_byte,
		              ri.srgb_byte, pi.expected_srgb_linear, pi.expected_srgb_nonlinear,
		              ri.unorm_byte, pi.expected_unorm, verdicts_[p]);
		LOGI("{}", line);
		file_text += std::string(line) + "\n";
	}
	LOGI("=========================================");
	file_text += "=========================================\n";

	std::ofstream f("srgb_blend_verdict.txt");
	if (f)
	{
		f << file_text;
	}
}

// ============================================================================
// Per-frame command buffer recording
// ============================================================================

void SrgbBlendVerification::build_command_buffers()
{
	const VkCommandBufferBeginInfo begin_info = vkb::initializers::command_buffer_begin_info();

	const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for (uint32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		VkCommandBuffer cmd = draw_cmd_buffers[i];
		VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

		VkImage swap_img = swapchain_buffers[i].image;

		// 1) Put swapchain image into TRANSFER_DST_OPTIMAL so we can blit into it.
		vkb::image_layout_transition(cmd, swap_img,
		                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                             0, VK_ACCESS_TRANSFER_WRITE_BIT,
		                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                             color_range);

		// 2) Blit sRGB offscreen image to the top half of the swapchain.
		VkImageBlit blit{};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {static_cast<int32_t>(kImageWidth), static_cast<int32_t>(kImageHeight), 1};
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height / 2), 1};

		vkCmdBlitImage(cmd,
		               srgb_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		               1, &blit, VK_FILTER_LINEAR);

		// 3) Blit the sRGB-format copy of the UNORM image to the bottom half.
		blit.dstOffsets[0] = {0, static_cast<int32_t>(height / 2), 0};
		blit.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
		vkCmdBlitImage(cmd,
		               display_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		               1, &blit, VK_FILTER_LINEAR);

		// 4) Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL for UI
		//    rendering (draw_ui for dynamic rendering starts its own pass).
		vkb::image_layout_transition(cmd, swap_img,
		                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                             color_range);

		// 5) Draw the UI overlay on top of the swapchain.
		draw_ui(cmd, i);

		// 6) Transition swapchain image to PRESENT_SRC_KHR.
		vkb::image_layout_transition(cmd, swap_img,
		                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
		                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		                             color_range);

		VK_CHECK(vkEndCommandBuffer(cmd));
	}
}

void SrgbBlendVerification::render(float delta_time)
{
	if (!prepared)
		return;

	ApiVulkanSample::prepare_frame();

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];
	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

	ApiVulkanSample::submit_frame();
}

bool SrgbBlendVerification::resize(const uint32_t w, const uint32_t h)
{
	ApiVulkanSample::resize(w, h);
	// Re-record the command buffers (the blit destination extent depends on
	// the swapchain size, which has now changed).
	build_command_buffers();
	return true;
}

// ============================================================================
// UI overlay
// ============================================================================

void SrgbBlendVerification::on_update_ui_overlay(vkb::Drawer &drawer)
{
	drawer.text("sRGB Hardware Blend Verification");
	drawer.text("Top: sRGB attachment (real sRGB blend path)");
	drawer.text("Bottom: UNORM attachment (control, no sRGB)");

	if (drawer.header("Predictions and measured values"))
	{
		drawer.text("P  bg   sRGB-act  lin-exp  nonlin-exp  UNORM-act  unorm-exp  verdict");
		for (uint32_t p = 0; p < kPatchCount; ++p)
		{
			const auto &pi = patches_[p];
			const auto &ri = results_[p];
			char line[200];
			std::snprintf(line, sizeof(line),
			              "%s  %3u   %3u       %3u      %3u         %3u       %3u       %s",
			              pi.name, pi.bg_srgb_byte,
			              ri.srgb_byte, pi.expected_srgb_linear, pi.expected_srgb_nonlinear,
			              ri.unorm_byte, pi.expected_unorm, verdicts_[p]);
			drawer.text(line);
		}
	}

	if (drawer.header("Verdict"))
	{
		bool all_linear = true;
		for (uint32_t p = 0; p < kPatchCount; ++p)
		{
			if (verdicts_[p] != std::string("LINEAR") && verdicts_[p] != std::string("near LINEAR"))
			{
				all_linear = false;
				break;
			}
		}
		if (all_linear && verified_)
		{
			drawer.text("CONFIRMED: hardware blends in LINEAR space on sRGB attachments.");
		}
		else if (verified_)
		{
			drawer.text("NOT confirmed: see per-patch verdicts above.");
		}
		else
		{
			drawer.text("(readback not yet performed)");
		}
	}
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<vkb::VulkanSampleC> create_srgb_blend_verification()
{
	return std::make_unique<SrgbBlendVerification>();
}
