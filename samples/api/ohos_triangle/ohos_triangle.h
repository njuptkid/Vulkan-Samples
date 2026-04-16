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

#include <string>
#include <vector>

// Must set platform macros before ANY Vulkan header is included
#ifndef VK_NO_PROTOTYPES
#    define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_OHOS_KHR
#    define VK_USE_PLATFORM_OHOS_KHR
#endif

#include <volk.h>

// vulkan_ohos.h forward-declares 'struct NativeWindow' as OHNativeWindow,
// and defines VkSurfaceCreateInfoOHOS / PFN_vkCreateSurfaceOHOS.
// Include it explicitly before external_window.h to establish the forward
// declaration first; external_window.h then provides the full struct body.
#include <vulkan/vulkan_ohos.h>

// OHOS NDK native window — must come AFTER vulkan_ohos.h's forward-decl
#include <native_window/external_window.h>

/**
 * @brief A self-contained Vulkan triangle renderer for OpenHarmony XComponent.
 *
 * This class does NOT inherit from any framework base class. It is designed
 * to be driven directly by the NAPI bridge (napi_init.cpp) and receives an
 * OHNativeWindow pointer to create its Vulkan surface. This matches the
 * XComponent lifecycle model.
 */
class OHOSTriangle
{
  public:
	OHOSTriangle()  = default;
	~OHOSTriangle() = default;

	/**
	 * @brief Initialize Vulkan and create all rendering resources.
	 * @param native_window The OHNativeWindow obtained from XComponent.
	 * @param width         Initial width of the render surface.
	 * @param height        Initial height of the render surface.
	 * @return true on success, false on failure.
	 */
	bool init(OHNativeWindow *native_window, uint32_t width, uint32_t height);

	/**
	 * @brief Render one frame. Safe to call in a loop.
	 */
	void render();

	/**
	 * @brief Handle surface resize.
	 */
	void resize(uint32_t width, uint32_t height);

	/**
	 * @brief Destroy all Vulkan resources. Must be called before destruction.
	 */
	void cleanup();

  private:
	// vkCreateSurfaceOHOS is a platform-specific extension NOT included
	// in the standard volk dispatch table; we load it manually.
	PFN_vkCreateSurfaceOHOS fp_vkCreateSurfaceOHOS = nullptr;

	// ------------------------------------------------------------------
	// Internal types
	// ------------------------------------------------------------------

	struct Vertex
	{
		float pos[3];
		float color[3];
	};

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
		std::vector<VkImage>     swapchain_images;
		std::vector<VkImageView> swapchain_image_views;
		std::vector<PerFrame>    per_frame;
		std::vector<VkSemaphore> recycled_semaphores;

		VkPipeline       pipeline        = VK_NULL_HANDLE;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkRenderPass     render_pass     = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> framebuffers;

		VkBuffer       vertex_buffer        = VK_NULL_HANDLE;
		VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;

		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	};

	// ------------------------------------------------------------------
	// Init helpers
	// ------------------------------------------------------------------
	void init_instance();
	void init_device();
	void init_surface(OHNativeWindow *native_window);
	void init_swapchain();
	void init_render_pass();
	void init_framebuffers();
	void init_vertex_buffer();
	void init_pipeline();
	void init_per_frame(PerFrame &frame);

	// ------------------------------------------------------------------
	// Teardown helpers
	// ------------------------------------------------------------------
	void teardown_per_frame(PerFrame &frame);
	void teardown_framebuffers();

	// ------------------------------------------------------------------
	// Per-frame rendering
	// ------------------------------------------------------------------
	VkResult acquire_next_image(uint32_t *image_index);
	void     record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);
	VkResult present_image(uint32_t image_index);

	// ------------------------------------------------------------------
	// Utilities
	// ------------------------------------------------------------------
	VkShaderModule  load_shader_spirv(const std::vector<uint8_t> &spirv);
	uint32_t        find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

	// ------------------------------------------------------------------
	// Embedded SPIR-V
	// ------------------------------------------------------------------
	static const std::vector<uint32_t> &get_vert_spirv();
	static const std::vector<uint32_t> &get_frag_spirv();

	Context  ctx_;
	bool     initialized_ = false;
};
