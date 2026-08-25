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

#pragma once

#include "api_vulkan_sample.h"

#include <array>
#include <memory>

/**
 * @brief Verifies that hardware blending on an sRGB color attachment linearizes
 *        the destination before blending, performs the blend in linear space,
 *        and re-encodes the result back to sRGB.
 *
 * The sample renders three patches with different sRGB-encoded backgrounds
 * (0, 51, 128 in 8-bit sRGB) and a white source at alpha 0.5 using standard
 * alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). Each test runs on BOTH an
 * sRGB color attachment AND a UNORM control attachment.
 *
 * The sRGB attachment distinguishes the two hypotheses:
 *  - Linear blend (spec compliant):   result is sRGB-encoded linear-blend value
 *  - Non-linear blend (no sRGB path): result is the raw sRGB-space blend value
 *
 * The UNORM attachment serves as a control: with no sRGB path, both hypotheses
 * collapse to the same value.
 *
 * Readback of the offscreen images at patch centers produces the actual bytes,
 * which are compared against the predicted bytes for both hypotheses. The UI
 * overlay shows the verdict per patch.
 */
class SrgbBlendVerification : public ApiVulkanSample
{
  public:
	SrgbBlendVerification();
	~SrgbBlendVerification() override;

	bool prepare(const vkb::ApplicationOptions &options) override;
	void render(float delta_time) override;
	void build_command_buffers() override;
	void request_gpu_features(vkb::core::PhysicalDeviceC &gpu) override;
	void on_update_ui_overlay(vkb::Drawer &drawer) override;
	bool resize(const uint32_t width, const uint32_t height) override;
	uint32_t get_api_version() const override
	{
		return VK_API_VERSION_1_2;
	}

	// Dynamic rendering bypasses render passes and framebuffers.
	void setup_render_pass() override
	{}
	void setup_framebuffer() override
	{}

  private:
	// Offscreen image dimensions: 3 patches of 256x256 laid out horizontally.
	static constexpr uint32_t kImageWidth  = 768;
	static constexpr uint32_t kImageHeight = 256;
	static constexpr uint32_t kPatchSize    = 256;
	static constexpr uint32_t kPatchCount   = 3;

	struct OffscreenImage
	{
		VkImage        image  = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView    view   = VK_NULL_HANDLE;
		VkFormat       format = VK_FORMAT_UNDEFINED;
	};

	struct PatchInfo
	{
		const char *name;                 // P1, P2, P3
		uint8_t     bg_srgb_byte;         // 0, 51, 128 - intended sRGB-encoded byte in framebuffer
		float       bg_linear;            // sRGB-decoded value (passed to shader as linear)
		uint8_t     expected_srgb_linear;   // predicted byte on sRGB attachment if linear hypothesis holds
		uint8_t     expected_srgb_nonlinear; // predicted byte on sRGB attachment if non-linear hypothesis holds
		uint8_t     expected_unorm;         // predicted byte on the UNORM control attachment (always)
	};

	struct PushConstants
	{
		float r, g, b, a;
	};

	struct ReadbackResult
	{
		uint8_t srgb_byte  = 0;    // byte read from the sRGB attachment
		uint8_t unorm_byte = 0;    // byte read from the UNORM control attachment
	};

	OffscreenImage srgb_image_;      // sRGB attachment: real sRGB blending (top half of screen)
	OffscreenImage unorm_image_;     // UNORM attachment: control case, no sRGB handling (bottom half source)
	OffscreenImage display_image_;   // sRGB-format copy of unorm_image_ bytes for correct display on sRGB swapchain

	VkPipeline       srgb_pipeline_   = VK_NULL_HANDLE;
	VkPipeline       unorm_pipeline_  = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout_  = VK_NULL_HANDLE;

	std::unique_ptr<vkb::core::BufferC> srgb_readback_buffer_;
	std::unique_ptr<vkb::core::BufferC> unorm_readback_buffer_;

	std::array<PatchInfo, kPatchCount>      patches_;
	std::array<ReadbackResult, kPatchCount> results_;
	std::array<const char *, kPatchCount>   verdicts_;
	bool                                    verified_ = false;

	// Image/pipeline creation
	void create_offscreen_image(OffscreenImage &img, VkFormat format, VkImageUsageFlags usage);
	void create_pipeline(VkFormat color_format, VkPipeline &out_pipeline);
	void create_pipelines();
	void destroy_offscreen_image(OffscreenImage &img);

	// One-time offscreen rendering and readback
	void render_offscreen_once(VkCommandBuffer cmd);
	void readback_and_verify();

	// Per-swapchain-image blit + UI recording
	void draw_patches(VkCommandBuffer cmd, VkPipeline pipeline);
};

std::unique_ptr<vkb::VulkanSampleC> create_srgb_blend_verification();
