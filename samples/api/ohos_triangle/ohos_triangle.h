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
#include "core/device.h"
#include "core/framebuffer.h"
#include "core/image.h"
#include "core/instance.h"
#include "core/physical_device.h"
#include "core/pipeline.h"
#include "core/pipeline_layout.h"
#include "core/render_pass.h"
#include "core/shader_module.h"
#include "platform/application.h"
#include "rendering/pipeline_state.h"
#include "rendering/render_target.h"
#include "resource_cache.h"
#include "semaphore_pool.h"
#include "core/buffer.h"
#include "core/command_pool.h"
#include "core/command_buffer.h"
#include "core/swapchain.h"
#include <vk_mem_alloc.h>

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
		std::shared_ptr<vkb::core::CommandBufferC> command_buffer;
		VkFence     queue_submit_fence  = VK_NULL_HANDLE;        // from FencePool
		VkSemaphore render_complete_sem = VK_NULL_HANDLE;        // from SemaphorePool
	};

	struct Context
	{
		VkInstance               instance       = VK_NULL_HANDLE;
		VkPhysicalDevice         gpu            = VK_NULL_HANDLE;
		VkDevice                 device         = VK_NULL_HANDLE;
		VkQueue                  queue          = VK_NULL_HANDLE;
		int32_t                  queue_index    = -1;
		VkSurfaceKHR             surface        = VK_NULL_HANDLE;
		SwapchainDimensions      swapchain_dim;
		VkPipeline               pipeline        = VK_NULL_HANDLE;
		VkPipelineLayout         pipeline_layout = VK_NULL_HANDLE;
		std::vector<PerFrame>    per_frame;
	};

	struct Vertex
	{
		float pos[3];
		float color[3];
	};

	VkBuffer       vertex_buffer        = VK_NULL_HANDLE;
	VmaAllocation  vertex_buffer_alloc  = VK_NULL_HANDLE;

	// Framework vertex buffer (replaces manual VMA above)
	std::unique_ptr<vkb::core::BufferC> fw_vertex_buffer;

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

	void init_pipeline();

	VkResult acquire_next_image(uint32_t *image);

	void render_triangle(uint32_t swapchain_index);

	VkResult present_image(uint32_t index);

	void init_framebuffers();

  private:
	Context context;

	// Framework wrappers — members are destroyed in REVERSE declaration order.
	// Required order: Instance destroyed LAST (after Device), Device destroyed after Pipeline objects.
	// So declaration order: Instance → GPU → Device → Pipeline objects
	std::unique_ptr<vkb::core::InstanceCpp>         fw_instance;
	std::unique_ptr<vkb::core::PhysicalDeviceCpp>   fw_gpu;
	std::unique_ptr<vkb::core::DeviceC>             fw_device;

	// Framework pipeline objects — owned by DeviceC's ResourceCache.
	// Raw pointers into the cache; no manual destruction needed.
	vkb::RenderPass       *fw_render_pass      = nullptr;
	vkb::PipelineLayout   *fw_pipeline_layout  = nullptr;
	vkb::GraphicsPipeline *fw_pipeline         = nullptr;
	vkb::ShaderModule     *fw_vert_shader      = nullptr;
	vkb::ShaderModule     *fw_frag_shader      = nullptr;

	// Framework sync primitive pools
	std::unique_ptr<vkb::SemaphorePool> fw_semaphore_pool;

	// Framework swapchain
	std::unique_ptr<vkb::Swapchain> fw_swapchain;

	// Framework RenderTarget + Framebuffer per swapchain image
	std::vector<std::unique_ptr<vkb::rendering::RenderTargetC>> fw_render_targets;
	std::vector<std::unique_ptr<vkb::Framebuffer>>              fw_framebuffers;
};

std::unique_ptr<vkb::Application> create_ohos_triangle();
