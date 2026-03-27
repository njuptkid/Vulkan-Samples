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

#pragma once

#include <array>
#include <vector>

#include "rendering/postprocessing_pipeline.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/perspective_camera.h"
#include "vulkan_sample.h"

/**
 * @brief Image Post Processing Sample
 *
 * This sample demonstrates multi-pass image post-processing effects:
 * - Grayscale conversion
 * - Gradient computation (Sobel edge detection)
 * - Gaussian blur
 * - Vignette effect
 *
 * Users can dynamically enable/disable each pass through the GUI.
 * Each pass reads from the previous pass's output, enabling chained effects.
 */
class ImagePostProcessing : public vkb::VulkanSampleC
{
  public:
	ImagePostProcessing();

	virtual ~ImagePostProcessing();

	virtual bool prepare(const vkb::ApplicationOptions &options) override;

	virtual void draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target) override;

	virtual void draw_gui() override;

  protected:
	virtual void prepare_render_context() override;

  private:
	// Pass type enumeration
	enum PassType
	{
		Grayscale         = 0,
		Gradient          = 1,
		Blur              = 2,
		Vignette          = 3,
		LuminanceGradient = 4,  // Luminance gradient with X in R, Y in G
		Count
	};

	// Render target attachment indices
	enum AttachmentIndex
	{
		Swapchain = 0,    // Final output for presentation
		Depth     = 1,    // Depth buffer (transient)
		Color     = 2,    // Scene color output (input to postprocessing)
		TempA     = 3,    // Intermediate attachment A (ping-pong)
		TempB     = 4,    // Intermediate attachment B (pong-ping)
		AttachmentCount
	};

	// GUI control variables
	std::array<bool, PassType::Count> enabled_passes{{false, false, false, false, false}};
	std::array<bool, PassType::Count> last_enabled_passes{{false, false, false, false, false}};

	// List of currently enabled pass types (for iteration)
	std::vector<PassType> active_pass_types;

	// Camera reference
	vkb::sg::PerspectiveCamera *camera{nullptr};

	// Scene render pipeline
	std::unique_ptr<vkb::rendering::RenderPipelineC> scene_pipeline;

	// Post-processing pipeline
	std::unique_ptr<vkb::PostProcessingPipeline> postprocessing_pipeline;

	// Load/store operations for scene render pass
	std::vector<vkb::LoadStoreInfo> scene_load_store;

	// Track actual layout of each attachment across frames
	// This is necessary because set_layout() only updates framework tracking,
	// not the actual Vulkan image layout
	std::array<VkImageLayout, AttachmentCount> attachment_layouts{{
	    VK_IMAGE_LAYOUT_UNDEFINED,  // Swapchain
	    VK_IMAGE_LAYOUT_UNDEFINED,  // Depth
	    VK_IMAGE_LAYOUT_UNDEFINED,  // Color
	    VK_IMAGE_LAYOUT_UNDEFINED,  // TempA
	    VK_IMAGE_LAYOUT_UNDEFINED   // TempB
	}};

	// Debug flag: render only the first frame (useful for layout debugging)
	bool render_only_first_frame{false};
	bool first_frame_rendered{false};

	/**
	 * @brief Create custom render target with intermediate color attachments
	 */
	std::unique_ptr<vkb::rendering::RenderTargetC> create_render_target(vkb::core::Image &&swapchain_image);

	/**
	 * @brief Setup post-processing pipeline with enabled passes
	 */
	void setup_postprocessing_pipeline();

	/**
	 * @brief Update the list of active pass types based on enabled_passes
	 */
	void update_active_pass_types();

	/**
	 * @brief Get the input attachment index for a given pass
	 * @param pass_index The index of the pass in the active_pass_types list
	 * @return The attachment index to read from
	 */
	uint32_t get_input_attachment(size_t pass_index) const;

	/**
	 * @brief Get the output attachment index for a given pass
	 * @param pass_index The index of the pass in the active_pass_types list
	 * @param total_passes Total number of passes
	 * @return The attachment index to write to
	 */
	uint32_t get_output_attachment(size_t pass_index, size_t total_passes) const;
};

std::unique_ptr<vkb::VulkanSampleC> create_image_postprocessing();
