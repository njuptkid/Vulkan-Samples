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

#include "ohos_triangle.h"

#include "common/vk_common.h"
#include "common/vk_initializers.h"
#include "core/allocated.h"
#include "core/hpp_debug.h"
#include "core/util/logging.hpp"
#include "filesystem/legacy.h"
#include "filesystem/filesystem.hpp"
#include "platform/window.h"

#if defined(OHOS)
#include <hilog/log.h>
#define OHOS_LOG_TAG "OHOSTri"
#define OHOS_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, OHOS_LOG_TAG, __VA_ARGS__)
#define OHOS_LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, OHOS_LOG_TAG, __VA_ARGS__)
#else
#define OHOS_LOGI(...) ((void)0)
#define OHOS_LOGE(...) ((void)0)
#endif

// Initialize vulkan.hpp dispatcher after volk loads function pointers.
// This is needed by framework core classes (Device, Pipeline, etc.) that
// use vk:: (C++ Vulkan) internally.
static void init_vulkan_hpp_dispatcher()
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
}

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                                     void                                       *user_data)
{
	(void) user_data;

	if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		LOGE("{} Validation: Error: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
		OHOS_LOGE("VVL Error: %s: %s", callback_data->pMessageIdName, callback_data->pMessage);
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		LOGE("{} Validation: Warning: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
		OHOS_LOGE("VVL Warning: %s: %s", callback_data->pMessageIdName, callback_data->pMessage);
	}
	else
	{
		LOGI("{} Validation: Info: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
		OHOS_LOGI("VVL Info: %s: %s", callback_data->pMessageIdName, callback_data->pMessage);
	}
	return VK_FALSE;
}
#endif

bool OHOSTriangle::validate_extensions(const std::vector<const char *>          &required,
                                       const std::vector<VkExtensionProperties> &available)
{
	for (auto extension : required)
	{
		bool found = false;
		for (auto &available_extension : available)
		{
			if (strcmp(available_extension.extensionName, extension) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return false;
		}
	}
	return true;
}

void OHOSTriangle::init_instance()
{
	LOGI("Initializing vulkan instance.");

	// volkInitialize() must happen before any vk:: calls.
	if (volkInitialize())
	{
		LOGE("volkInitialize() failed — cannot load Vulkan loader.");
		throw std::runtime_error("Failed to initialize volk.");
	}

	// Initialize vulkan.hpp dispatcher with vkGetInstanceProcAddr.
	// Required before InstanceCpp can call vk::enumerateInstanceVersion() etc.
	init_vulkan_hpp_dispatcher();

	// Build extension request map
	std::unordered_map<std::string, vkb::RequestMode> instance_extensions = {
	    {VK_KHR_SURFACE_EXTENSION_NAME, vkb::RequestMode::Required},
#if defined(VK_USE_PLATFORM_OHOS_KHR)
	    {VK_OHOS_SURFACE_EXTENSION_NAME, vkb::RequestMode::Required},
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	    {VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, vkb::RequestMode::Required},
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
	    {VK_KHR_WIN32_SURFACE_EXTENSION_NAME, vkb::RequestMode::Required},
#endif
	};

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	instance_extensions.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, vkb::RequestMode::Optional);
#endif

	// Build layer request map
	std::unordered_map<std::string, vkb::RequestMode> instance_layers;
#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	instance_layers.emplace("VK_LAYER_KHRONOS_validation", vkb::RequestMode::Optional);
#endif

	// InstanceCpp full constructor handles: extension/layer validation,
	// VkInstance creation, vulkan.hpp dispatcher init, volkLoadInstance.
	fw_instance = std::make_unique<vkb::core::InstanceCpp>(
	    "OHOS Triangle",
	    VK_API_VERSION_1_1,
	    instance_layers,
	    instance_extensions,
	    [](std::vector<std::string> const &, std::vector<std::string> const &) -> void const * { return nullptr; },
	    [](std::vector<std::string> const &) -> vk::InstanceCreateFlags { return {}; });

	context.instance = static_cast<VkInstance>(fw_instance->get_handle());
	OHOS_LOGI("init_instance: InstanceCpp full constructor OK");

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	// Create debug messenger to redirect validation output to hilog
	if (fw_instance->is_extension_enabled(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
	{
		VkDebugUtilsMessengerCreateInfoEXT debug_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
		debug_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		debug_info.pfnUserCallback = debug_callback;
		VK_CHECK(vkCreateDebugUtilsMessengerEXT(context.instance, &debug_info, nullptr, &debug_messenger));
		OHOS_LOGI("init_instance: debug messenger created");
	}
#endif
}

void OHOSTriangle::init_device()
{
	LOGI("Selecting physical device and queue family.");

	// Enumerate physical devices and select one with graphics + present support.
	// VkDevice creation is handled by DeviceC full constructor.
	auto physical_devices = static_cast<vk::Instance>(context.instance).enumeratePhysicalDevices();
	if (physical_devices.empty())
	{
		throw std::runtime_error("No physical device found.");
	}

	for (auto &phys_dev : physical_devices)
	{
		auto queue_family_props = phys_dev.getQueueFamilyProperties();
		for (uint32_t j = 0; j < queue_family_props.size(); j++)
		{
			if ((queue_family_props[j].queueFlags & vk::QueueFlagBits::eGraphics) &&
			    phys_dev.getSurfaceSupportKHR(j, static_cast<vk::SurfaceKHR>(context.surface)))
			{
				context.gpu        = static_cast<VkPhysicalDevice>(phys_dev);
				context.queue_index = static_cast<int32_t>(j);
				break;
			}
		}
		if (context.queue_index >= 0)
		{
			break;
		}
	}

	if (context.queue_index < 0)
	{
		throw std::runtime_error("Did not find suitable device with a queue that supports graphics and presentation.");
	}

	OHOS_LOGI("init_device: selected GPU, queue_family=%{public}d", context.queue_index);
}

void OHOSTriangle::init_vertex_buffer()
{
	const Vertex vertices[] = {
	    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	    {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};

	VkDeviceSize buffer_size = sizeof(vertices);

	// Use framework Buffer with VMA
	fw_vertex_buffer = std::make_unique<vkb::core::BufferC>(
	    *fw_device, buffer_size,
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	    VMA_MEMORY_USAGE_AUTO,
	    VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	// Copy vertex data
	fw_vertex_buffer->update(vertices, buffer_size);
	vertex_buffer = fw_vertex_buffer->get_handle();

	OHOS_LOGI("init_vertex_buffer: framework Buffer created (%zu bytes)", (size_t) buffer_size);
}

void OHOSTriangle::init_per_frame(PerFrame &per_frame)
{
	// Request fence from DeviceC's internal pool (created in signaled state)
	per_frame.queue_submit_fence = fw_device->get_fence_pool().request_fence();

	// Request command buffer from DeviceC's internal command pool
	per_frame.command_buffer = fw_device->get_command_pool().request_command_buffer();

	// Request a semaphore for render completion signaling
	per_frame.render_complete_sem = fw_semaphore_pool->request_semaphore();
}

void OHOSTriangle::teardown_per_frame(PerFrame &per_frame)
{
	// Fence, semaphore, command buffer all managed by pools — just clear handles
	per_frame.queue_submit_fence = VK_NULL_HANDLE;
	per_frame.render_complete_sem = VK_NULL_HANDLE;
	per_frame.command_buffer.reset();
}

void OHOSTriangle::init_swapchain()
{
	// Teardown old per-frame data if recreating
	if (!context.per_frame.empty())
	{
		for (auto &pf : context.per_frame)
		{
			teardown_per_frame(pf);
		}
		context.per_frame.clear();
	}

	VkExtent2D extent{context.swapchain_dim.width, context.swapchain_dim.height};

	// Create framework swapchain — handles surface queries, format selection,
	// image creation automatically.
	std::vector<VkPresentModeKHR> present_modes = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
	std::set<VkImageUsageFlagBits> usage_flags = {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};

	fw_swapchain = std::make_unique<vkb::Swapchain>(
	    *fw_device, context.surface,
	    VK_PRESENT_MODE_FIFO_KHR,
	    present_modes,
	    std::vector<VkSurfaceFormatKHR>{},        // use default format priority
	    extent,
	    3,
	    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	    usage_flags);

	context.swapchain_dim.width  = fw_swapchain->get_extent().width;
	context.swapchain_dim.height = fw_swapchain->get_extent().height;
	context.swapchain_dim.format = fw_swapchain->get_format();

	auto &images = fw_swapchain->get_images();
	OHOS_LOGI("init_swapchain: %ux%u, %zu images",
	          context.swapchain_dim.width, context.swapchain_dim.height, images.size());

	context.per_frame.resize(images.size());
	for (size_t i = 0; i < images.size(); i++)
	{
		init_per_frame(context.per_frame[i]);
	}
}

void OHOSTriangle::init_render_pass()
{
	// Use framework RenderPass exclusively.
	// Framework sets finalLayout=COLOR_ATTACHMENT_OPTIMAL.
	// After vkCmdEndRenderPass, render_triangle() transitions to PRESENT_SRC_KHR
	// via an explicit pipeline barrier before present.
	vkb::rendering::AttachmentC color_attachment{};
	color_attachment.format  = context.swapchain_dim.format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	std::vector<vkb::rendering::AttachmentC> attachments = {color_attachment};
	std::vector<vkb::LoadStoreInfo> load_store = {{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE}};

	vkb::SubpassInfo subpass_info{};
	subpass_info.output_attachments = {0};
	std::vector<vkb::SubpassInfo> subpasses = {subpass_info};

	fw_render_pass = &fw_device->get_resource_cache().request_render_pass(attachments, load_store, subpasses);
	OHOS_LOGI("init_render_pass: framework RenderPass (cached)");
}

void OHOSTriangle::init_pipeline()
{
	auto &cache = fw_device->get_resource_cache();

	// Request shader modules from cache (auto SPIRV reflection)
	vkb::ShaderVariant empty_variant{};
	vkb::ShaderSource  vert_source("ohos_triangle/glsl/triangle.vert.spv");
	vkb::ShaderSource  frag_source("ohos_triangle/glsl/triangle.frag.spv");

	fw_vert_shader = &cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, vert_source, empty_variant);
	OHOS_LOGI("init_pipeline: vertex shader module (cached)");

	fw_frag_shader = &cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, frag_source, empty_variant);
	OHOS_LOGI("init_pipeline: fragment shader module (cached)");

	// PipelineLayout auto-creates descriptor set layouts from shader reflection
	fw_pipeline_layout = &cache.request_pipeline_layout({fw_vert_shader, fw_frag_shader});
	context.pipeline_layout = fw_pipeline_layout->get_handle();
	OHOS_LOGI("init_pipeline: pipeline layout (cached)");

	// Configure PipelineState
	vkb::PipelineState pipeline_state{};

	// Vertex input
	vkb::VertexInputState vertex_input{};
	vertex_input.bindings   = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
	vertex_input.attributes = {
	    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
	    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
	};
	pipeline_state.set_vertex_input_state(vertex_input);

	// Input assembly
	pipeline_state.set_input_assembly_state({VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE});

	// Rasterization
	vkb::RasterizationState raster{};
	raster.cull_mode  = VK_CULL_MODE_NONE;
	raster.front_face = VK_FRONT_FACE_CLOCKWISE;
	pipeline_state.set_rasterization_state(raster);

	// Viewport
	pipeline_state.set_viewport_state({1, 1});

	// Multisample
	pipeline_state.set_multisample_state({VK_SAMPLE_COUNT_1_BIT});

	// Depth stencil (disabled)
	pipeline_state.set_depth_stencil_state({VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS});

	// Color blend
	vkb::ColorBlendState blend{};
	vkb::ColorBlendAttachmentState blend_attachment{};
	blend_attachment.blend_enable           = VK_FALSE;
	blend_attachment.color_write_mask       = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend.attachments                      = {blend_attachment};
	pipeline_state.set_color_blend_state(blend);

	// Set pipeline layout and render pass
	pipeline_state.set_pipeline_layout(*fw_pipeline_layout);
	pipeline_state.set_render_pass(*fw_render_pass);

	// Request pipeline from cache
	fw_pipeline = &cache.request_graphics_pipeline(pipeline_state);
	context.pipeline = fw_pipeline->get_handle();
	OHOS_LOGI("init_pipeline: graphics pipeline (cached)");
}

void OHOSTriangle::init_framebuffers()
{
	// Clear old render targets and framebuffers (framework handles VkFramebuffer/VkImageView cleanup)
	fw_framebuffers.clear();
	fw_render_targets.clear();

	// Wrap swapchain images in framework Image objects and create RenderTarget + Framebuffer
	auto &swapchain_images = fw_swapchain->get_images();
	VkExtent3D extent3d{context.swapchain_dim.width, context.swapchain_dim.height, 1};

	for (auto &img : swapchain_images)
	{
		// Wrap existing swapchain VkImage — no VMA allocation, destructor is safe
		auto wrapped_image = std::make_unique<vkb::core::Image>(
		    *fw_device, img, extent3d,
		    context.swapchain_dim.format,
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

		// RenderTarget takes ownership of Image, auto-creates ImageView
		std::vector<vkb::core::Image> images;
		images.push_back(std::move(*wrapped_image));
		auto render_target = std::make_unique<vkb::rendering::RenderTargetC>(std::move(images));

		// Framebuffer from RenderTarget + framework RenderPass (compatible with manual one)
		auto framebuffer = std::make_unique<vkb::Framebuffer>(
		    *fw_device, *render_target, *fw_render_pass);

		fw_render_targets.push_back(std::move(render_target));
		fw_framebuffers.push_back(std::move(framebuffer));
	}

	OHOS_LOGI("init_framebuffers: %zu framework Framebuffers created", fw_framebuffers.size());
}

VkResult OHOSTriangle::acquire_next_image(uint32_t *image)
{
	VkSemaphore acquire_semaphore = fw_semaphore_pool->request_semaphore();

	VkResult res = vkAcquireNextImageKHR(context.device, fw_swapchain->get_handle(),
	                                      UINT64_MAX, acquire_semaphore, VK_NULL_HANDLE, image);
	if (res != VK_SUCCESS)
	{
		fw_semaphore_pool->release_owned_semaphore(acquire_semaphore);
		return res;
	}

	if (context.per_frame[*image].queue_submit_fence != VK_NULL_HANDLE)
	{
		vkWaitForFences(context.device, 1, &context.per_frame[*image].queue_submit_fence, true, UINT64_MAX);
		vkResetFences(context.device, 1, &context.per_frame[*image].queue_submit_fence);
	}

	// Store acquire semaphore for this frame
	context.per_frame[*image].render_complete_sem = acquire_semaphore;

	return VK_SUCCESS;
}

void OHOSTriangle::render_triangle(uint32_t swapchain_index)
{
	VkFramebuffer framebuffer = fw_framebuffers[swapchain_index]->get_handle();
	VkCommandBuffer cmd       = context.per_frame[swapchain_index].command_buffer->get_handle();

	auto begin_info = vkb::initializers::command_buffer_begin_info();
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

	// Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL to match the
	// framework RenderPass's initialLayout. Swapchain images arrive in
	// UNDEFINED (first frame) or PRESENT_SRC_KHR (subsequent frames).
	VkImageMemoryBarrier pre_barrier{};
	pre_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	pre_barrier.srcAccessMask                   = 0;
	pre_barrier.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	pre_barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
	pre_barrier.newLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	pre_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.image                           = fw_swapchain->get_images()[swapchain_index];
	pre_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	pre_barrier.subresourceRange.baseMipLevel   = 0;
	pre_barrier.subresourceRange.levelCount     = 1;
	pre_barrier.subresourceRange.baseArrayLayer = 0;
	pre_barrier.subresourceRange.layerCount     = 1;

	vkCmdPipelineBarrier(cmd,
	                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                      0, 0, nullptr, 0, nullptr, 1, &pre_barrier);

	// Use framework RenderPass (finalLayout=COLOR_ATTACHMENT_OPTIMAL)
	auto rp_begin         = vkb::initializers::render_pass_begin_info();
	rp_begin.renderPass  = fw_render_pass->get_handle();
	rp_begin.framebuffer = framebuffer;
	rp_begin.renderArea  = {{0, 0}, {context.swapchain_dim.width, context.swapchain_dim.height}};

	VkClearValue clear_value = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	rp_begin.clearValueCount = 1;
	rp_begin.pClearValues    = &clear_value;

	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0, 0, (float) context.swapchain_dim.width, (float) context.swapchain_dim.height, 0, 1};
	vkCmdSetViewport(cmd, 0, 1, &vp);

	VkRect2D scissor = {{0, 0}, {context.swapchain_dim.width, context.swapchain_dim.height}};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, context.pipeline);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);

	// Transition swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR.
	// The framework RenderPass ends with finalLayout=COLOR_ATTACHMENT_OPTIMAL,
	// but vkQueuePresentKHR requires PRESENT_SRC_KHR.
	VkImageMemoryBarrier barrier{};
	barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask                   = 0;
	barrier.oldLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
	barrier.image                           = fw_swapchain->get_images()[swapchain_index];
	barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel   = 0;
	barrier.subresourceRange.levelCount     = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = 1;

	vkCmdPipelineBarrier(cmd,
	                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	                      0, 0, nullptr, 0, nullptr, 1, &barrier);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

VkResult OHOSTriangle::present_image(uint32_t index)
{
	// The acquire semaphore (stored in render_complete_sem by acquire_next_image)
	// is used as the wait semaphore for the queue submit.
	VkSemaphore acquire_sem = context.per_frame[index].render_complete_sem;

	// Request a new semaphore to signal render completion
	VkSemaphore render_done_sem = fw_semaphore_pool->request_semaphore();

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit_info = vkb::initializers::submit_info();
	submit_info.waitSemaphoreCount   = 1;
	submit_info.pWaitSemaphores      = &acquire_sem;
	submit_info.pWaitDstStageMask    = &wait_stage;
	submit_info.commandBufferCount   = 1;
	submit_info.pCommandBuffers      = &context.per_frame[index].command_buffer->get_handle();
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = &render_done_sem;

	VK_CHECK(context.queue->submit({submit_info}, context.per_frame[index].queue_submit_fence));

	VkSwapchainKHR swapchain_handle = fw_swapchain->get_handle();
	VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores    = &render_done_sem;
	present.swapchainCount     = 1;
	present.pSwapchains        = &swapchain_handle;
	present.pImageIndices      = &index;

	return context.queue->present(present);
}

// ---------------------------------------------------------------------------
// vkb::Application interface
// ---------------------------------------------------------------------------

OHOSTriangle::OHOSTriangle()
{
}

OHOSTriangle::~OHOSTriangle()
{
	if (context.device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(context.device);
	}

	// Framework Framebuffer + RenderTarget auto-cleanup
	fw_framebuffers.clear();
	fw_render_targets.clear();
	// RenderPass owned by ResourceCache (via fw_device)
	fw_swapchain.reset();
	for (auto &pf : context.per_frame)
	{
		teardown_per_frame(pf);
	}
	fw_vertex_buffer.reset();
	// Destroy semaphore pool (before Device since it uses VkDevice)
	fw_semaphore_pool.reset();
	// DeviceC destructor: clears resource cache, internal fence/command pools,
	// calls vkb::allocated::shutdown(), and destroys VkDevice.
	fw_device.reset();
	// Surface must be destroyed before Instance
	if (context.surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(context.instance, context.surface, nullptr);
	}
	// InstanceCpp destructor destroys VkInstance.
	fw_gpu.reset();
#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	if (debug_messenger != VK_NULL_HANDLE)
	{
		vkDestroyDebugUtilsMessengerEXT(context.instance, debug_messenger, nullptr);
	}
#endif
	fw_instance.reset();
}

bool OHOSTriangle::prepare(const vkb::ApplicationOptions &options)
{
	assert(options.window != nullptr);
	assert(options.window->get_window_mode() != vkb::Window::Mode::Headless);

	OHOS_LOGI("prepare() START");

	init_instance();
	OHOS_LOGI("prepare: init_instance OK");

	context.surface = options.window->create_surface(context.instance, nullptr);
	auto &extent    = options.window->get_extent();
	context.swapchain_dim.width  = extent.width;
	context.swapchain_dim.height = extent.height;

	if (!context.surface)
	{
		OHOS_LOGE("prepare: Failed to create window surface");
		throw std::runtime_error("Failed to create window surface.");
	}
	OHOS_LOGI("prepare: surface created");

	init_device();
	OHOS_LOGI("prepare: init_device OK");

	// fw_instance already created by init_instance() full constructor.

	fw_gpu = std::make_unique<vkb::core::PhysicalDeviceCpp>(
	    *fw_instance,
	    static_cast<vk::PhysicalDevice>(context.gpu));
	OHOS_LOGI("prepare: fw_gpu OK");

	// DeviceC full constructor creates VkDevice, queues, VMA allocator,
	// internal command pool, and internal fence pool.
	auto hpp_debug_utils = std::make_unique<vkb::core::HPPDummyDebugUtils>();
	std::unordered_map<const char *, bool> device_extensions = {
	    {VK_KHR_SWAPCHAIN_EXTENSION_NAME, false},
	};

	fw_device = std::make_unique<vkb::core::DeviceC>(
	    reinterpret_cast<vkb::core::PhysicalDeviceC &>(*fw_gpu),
	    context.surface,
	    std::unique_ptr<vkb::DebugUtils>(reinterpret_cast<vkb::DebugUtils *>(hpp_debug_utils.release())),
	    device_extensions,
	    [](vkb::core::PhysicalDeviceC &) {});
	OHOS_LOGI("prepare: fw_device OK (full constructor)");

	// Get device/queue handles from framework Device
	context.device = static_cast<VkDevice>(fw_device->get_handle());
	context.queue  = &fw_device->get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);
	OHOS_LOGI("prepare: device/queue handles obtained");

	// Initialize vulkan.hpp dispatcher with the device (third step).
	// This loads device-level function pointers for vk:: calls.
	VULKAN_HPP_DEFAULT_DISPATCHER.init(static_cast<vk::Device>(fw_device->get_handle()));
	// Also load device-level function pointers for volk C calls.
	volkLoadDevice(context.device);

	// Create semaphore pool (DeviceC has internal fence/command pools but no semaphore pool)
	fw_semaphore_pool = std::make_unique<vkb::SemaphorePool>(*fw_device);

	init_vertex_buffer();
	OHOS_LOGI("prepare: init_vertex_buffer OK");
	init_swapchain();
	OHOS_LOGI("prepare: init_swapchain OK");
	init_render_pass();
	OHOS_LOGI("prepare: init_render_pass OK");
	init_pipeline();
	OHOS_LOGI("prepare: init_pipeline OK");
	init_framebuffers();
	OHOS_LOGI("prepare: init_framebuffers OK");

	OHOS_LOGI("prepare() COMPLETE");
	return true;
}

void OHOSTriangle::update(float delta_time)
{
	uint32_t index;

	auto res = acquire_next_image(&index);
	if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		resize(context.swapchain_dim.width, context.swapchain_dim.height);
		res = acquire_next_image(&index);
	}
	if (res != VK_SUCCESS)
	{
		context.queue->wait_idle();
		return;
	}

	render_triangle(index);
	res = present_image(index);

	if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		resize(context.swapchain_dim.width, context.swapchain_dim.height);
	}
	else if (res != VK_SUCCESS)
	{
		LOGE("Failed to present swapchain image.");
	}
}

bool OHOSTriangle::resize(const uint32_t, const uint32_t)
{
	if (context.device == VK_NULL_HANDLE)
	{
		return false;
	}

	VkSurfaceCapabilitiesKHR surface_props;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.gpu, context.surface, &surface_props));

	if (surface_props.currentExtent.width == context.swapchain_dim.width &&
	    surface_props.currentExtent.height == context.swapchain_dim.height)
	{
		return false;
	}

	vkDeviceWaitIdle(context.device);
	LOGI("Resizing swapchain...");

	// Framework Framebuffer + RenderTarget auto-cleanup
	fw_framebuffers.clear();
	fw_render_targets.clear();

	// Reset framework pipeline objects — clear cache instead of individual resets
	fw_device->get_resource_cache().clear_pipelines();
	fw_pipeline        = nullptr;
	fw_pipeline_layout = nullptr;
	fw_render_pass     = nullptr;
	fw_vert_shader     = nullptr;
	fw_frag_shader     = nullptr;
	context.pipeline        = VK_NULL_HANDLE;
	context.pipeline_layout = VK_NULL_HANDLE;

	init_swapchain();
	init_render_pass();
	init_pipeline();
	init_framebuffers();
	return true;
}

std::unique_ptr<vkb::Application> create_ohos_triangle()
{
	return std::make_unique<OHOSTriangle>();
}
