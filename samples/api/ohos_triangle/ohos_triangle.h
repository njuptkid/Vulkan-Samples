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

#include "common/vk_common.h"
#include "core/instance.h"
#include "platform/application.h"

/**
 * @brief A self-contained triangle sample using vkb::Application base class.
 *
 * This sample follows the same pattern as HelloTriangle, using the framework's
 * Platform/Window for surface creation and vkb::fs for shader loading.
 * It is designed to be driven by the OHOS NAPI bridge via OHOSPlatform.
 */
class OHOSTriangle : public vkb::Application
{
	struct SwapchainDimensions
	{
		uint32_t width  = 0;
		uint32_t height = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	struct PerFrame
	{
		VkFence         queue_submit_fence          = VK_NULL_HANDLE;
		VkCommandPool   primary_command_pool        = VK_NULL_HANDLE;
		VkCommandBuffer primary_command_buffer      = VK_NULL_HANDLE;
		VkSemaphore     swapchain_acquire_semaphore = VK_NULL_HANDLE;
		VkSemaphore     swapchain_release_semaphore = VK_NULL_HANDLE;
	};

	struct Context
	{
		VkInstance               instance       = VK_NULL_HANDLE;
		VkPhysicalDevice         gpu            = VK_NULL_HANDLE;
		VkDevice                 device         = VK_NULL_HANDLE;
		VkQueue                  queue          = VK_NULL_HANDLE;
		int32_t                  queue_index    = -1;
		VkSurfaceKHR             surface        = VK_NULL_HANDLE;
		VkSwapchainKHR           swapchain      = VK_NULL_HANDLE;
		SwapchainDimensions      swapchain_dim;
		std::vector<VkImageView> swapchain_image_views;
		std::vector<VkFramebuffer> framebuffers;
		VkRenderPass             render_pass     = VK_NULL_HANDLE;
		VkPipeline               pipeline        = VK_NULL_HANDLE;
		VkPipelineLayout         pipeline_layout = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
		std::vector<PerFrame>    per_frame;
		std::vector<VkSemaphore> recycled_semaphores;
	};

	struct Vertex
	{
		float pos[3];
		float color[3];
	};

	VkBuffer       vertex_buffer        = VK_NULL_HANDLE;
	VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;

  public:
	OHOSTriangle();

	virtual ~OHOSTriangle();

	virtual bool prepare(const vkb::ApplicationOptions &options) override;

	virtual void update(float delta_time) override;

	virtual bool resize(const uint32_t width, const uint32_t height) override;

	bool validate_extensions(const std::vector<const char *>          &required,
	                         const std::vector<VkExtensionProperties> &available);

	void init_instance();

	void init_device();

	void init_vertex_buffer();

	void init_per_frame(PerFrame &per_frame);

	void teardown_per_frame(PerFrame &per_frame);

	void init_swapchain();

	void init_render_pass();

	VkShaderModule load_shader_module(const std::string &path);

	void init_pipeline();

	uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

	VkResult acquire_next_image(uint32_t *image);

	void render_triangle(uint32_t swapchain_index);

	VkResult present_image(uint32_t index);

	void init_framebuffers();

  private:
	Context context;
};

std::unique_ptr<vkb::Application> create_ohos_triangle();
