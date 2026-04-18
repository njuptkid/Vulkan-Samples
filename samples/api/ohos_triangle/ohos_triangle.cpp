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
		LOGE("{} Validation Layer: Error: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		LOGE("{} Validation Layer: Warning: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
	}
	else
	{
		LOGI("{} Validation Layer: Information: {}: {}", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
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

	if (volkInitialize())
	{
		LOGE("volkInitialize() failed — cannot load Vulkan loader.");
		throw std::runtime_error("Failed to initialize volk.");
	}

	// Initialize vulkan.hpp dispatcher using volk's loaded function pointers.
	// Required by framework core classes (Device, Pipeline, etc.) that use vk:: internally.
	init_vulkan_hpp_dispatcher();

	uint32_t instance_extension_count;
	VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr));

	std::vector<VkExtensionProperties> available_instance_extensions(instance_extension_count);
	VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, available_instance_extensions.data()));

	std::vector<const char *> required_instance_extensions{VK_KHR_SURFACE_EXTENSION_NAME};

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	bool has_debug_utils = false;
	for (const auto &ext : available_instance_extensions)
	{
		if (strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
		{
			has_debug_utils = true;
			required_instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			break;
		}
	}
	if (!has_debug_utils)
	{
		LOGW("{} not supported or available", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
#endif

#if defined(VK_USE_PLATFORM_OHOS_KHR)
	required_instance_extensions.push_back(VK_OHOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	required_instance_extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
	required_instance_extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

	if (!validate_extensions(required_instance_extensions, available_instance_extensions))
	{
		throw std::runtime_error("Required instance extensions are missing.");
	}

	std::vector<const char *> requested_instance_layers{};

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	char const *validationLayer = "VK_LAYER_KHRONOS_validation";
	uint32_t instance_layer_count;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr));
	std::vector<VkLayerProperties> supported_instance_layers(instance_layer_count);
	VK_CHECK(vkEnumerateInstanceLayerProperties(&instance_layer_count, supported_instance_layers.data()));
	for (auto const &lp : supported_instance_layers)
	{
		if (strcmp(lp.layerName, validationLayer) == 0)
		{
			requested_instance_layers.push_back(validationLayer);
			LOGI("Enabled Validation Layer {}", validationLayer);
			break;
		}
	}
#endif

	VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.pApplicationName   = "OHOS Triangle";
	app.pEngineName        = "Vulkan Samples";
	app.apiVersion         = VK_API_VERSION_1_1;

	VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	instance_info.pApplicationInfo        = &app;
	instance_info.enabledLayerCount       = static_cast<uint32_t>(requested_instance_layers.size());
	instance_info.ppEnabledLayerNames     = requested_instance_layers.data();
	instance_info.enabledExtensionCount   = static_cast<uint32_t>(required_instance_extensions.size());
	instance_info.ppEnabledExtensionNames = required_instance_extensions.data();

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
	if (has_debug_utils)
	{
		debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debug_utils_create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		debug_utils_create_info.pfnUserCallback = debug_callback;
		instance_info.pNext                     = &debug_utils_create_info;
	}
#endif

	VK_CHECK(vkCreateInstance(&instance_info, nullptr, &context.instance));
	volkLoadInstance(context.instance);

	// Initialize vulkan.hpp dispatcher with the created instance.
	// This loads instance-level and device-level function pointers.
	VULKAN_HPP_DEFAULT_DISPATCHER.init(static_cast<vk::Instance>(context.instance));

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	if (has_debug_utils)
	{
		VK_CHECK(vkCreateDebugUtilsMessengerEXT(context.instance, &debug_utils_create_info, nullptr, &context.debug_messenger));
	}
#endif
}

void OHOSTriangle::init_device()
{
	LOGI("Initializing vulkan device.");

	uint32_t gpu_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &gpu_count, nullptr));
	if (gpu_count < 1)
	{
		throw std::runtime_error("No physical device found.");
	}

	std::vector<VkPhysicalDevice> gpus(gpu_count);
	VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &gpu_count, gpus.data()));

	for (size_t i = 0; i < gpu_count && (context.queue_index < 0); i++)
	{
		context.gpu = gpus[i];

		uint32_t queue_family_count;
		vkGetPhysicalDeviceQueueFamilyProperties(context.gpu, &queue_family_count, nullptr);
		std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context.gpu, &queue_family_count, queue_family_properties.data());

		for (uint32_t j = 0; j < queue_family_count; j++)
		{
			VkBool32 supports_present;
			vkGetPhysicalDeviceSurfaceSupportKHR(context.gpu, j, context.surface, &supports_present);

			if ((queue_family_properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supports_present)
			{
				context.queue_index = static_cast<int32_t>(j);
				break;
			}
		}
	}

	if (context.queue_index < 0)
	{
		throw std::runtime_error("Did not find suitable device with a queue that supports graphics and presentation.");
	}

	uint32_t device_extension_count;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(context.gpu, nullptr, &device_extension_count, nullptr));
	std::vector<VkExtensionProperties> device_extensions(device_extension_count);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(context.gpu, nullptr, &device_extension_count, device_extensions.data()));

	std::vector<const char *> required_device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	if (!validate_extensions(required_device_extensions, device_extensions))
	{
		throw std::runtime_error("Required device extensions are missing.");
	}

	const float queue_priority = 0.5f;
	VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queue_info.queueFamilyIndex = static_cast<uint32_t>(context.queue_index);
	queue_info.queueCount       = 1;
	queue_info.pQueuePriorities = &queue_priority;

	VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	device_info.queueCreateInfoCount    = 1;
	device_info.pQueueCreateInfos       = &queue_info;
	device_info.enabledExtensionCount   = static_cast<uint32_t>(required_device_extensions.size());
	device_info.ppEnabledExtensionNames = required_device_extensions.data();

	VK_CHECK(vkCreateDevice(context.gpu, &device_info, nullptr, &context.device));
	volkLoadDevice(context.device);

	vkGetDeviceQueue(context.device, context.queue_index, 0, &context.queue);
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
	// Request fence from pool (created in signaled state)
	per_frame.queue_submit_fence = fw_fence_pool->request_fence();

	auto pool_info             = vkb::initializers::command_pool_create_info();
	pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool_info.queueFamilyIndex = static_cast<uint32_t>(context.queue_index);
	VK_CHECK(vkCreateCommandPool(context.device, &pool_info, nullptr, &per_frame.primary_command_pool));

	auto cmd_info = vkb::initializers::command_buffer_allocate_info(per_frame.primary_command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
	VK_CHECK(vkAllocateCommandBuffers(context.device, &cmd_info, &per_frame.primary_command_buffer));

	// Request a semaphore for render completion signaling
	per_frame.render_complete_sem = fw_semaphore_pool->request_semaphore();
}

void OHOSTriangle::teardown_per_frame(PerFrame &per_frame)
{
	// Fence and semaphore managed by pools — no manual destroy needed
	per_frame.queue_submit_fence = VK_NULL_HANDLE;
	per_frame.render_complete_sem = VK_NULL_HANDLE;

	if (per_frame.primary_command_buffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(context.device, per_frame.primary_command_pool, 1, &per_frame.primary_command_buffer);
		per_frame.primary_command_buffer = VK_NULL_HANDLE;
	}
	if (per_frame.primary_command_pool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(context.device, per_frame.primary_command_pool, nullptr);
		per_frame.primary_command_pool = VK_NULL_HANDLE;
	}
}

void OHOSTriangle::init_swapchain()
{
	VkSurfaceCapabilitiesKHR surface_props;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.gpu, context.surface, &surface_props));

	VkSurfaceFormatKHR format = vkb::select_surface_format(context.gpu, context.surface);

	VkExtent2D swapchain_size{};
	if (surface_props.currentExtent.width == 0xFFFFFFFF)
	{
		swapchain_size.width  = context.swapchain_dim.width;
		swapchain_size.height = context.swapchain_dim.height;
	}
	else
	{
		swapchain_size = surface_props.currentExtent;
	}

	VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;

	uint32_t desired_images = surface_props.minImageCount + 1;
	if ((surface_props.maxImageCount > 0) && (desired_images > surface_props.maxImageCount))
	{
		desired_images = surface_props.maxImageCount;
	}

	VkSurfaceTransformFlagBitsKHR pre_transform;
	if (surface_props.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
	{
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else
	{
		pre_transform = surface_props.currentTransform;
	}

	VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (surface_props.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
	{
		composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}
	else if (surface_props.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
	{
		composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	}

	VkSwapchainKHR old_swapchain = context.swapchain;

	VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	info.surface          = context.surface;
	info.minImageCount    = desired_images;
	info.imageFormat      = format.format;
	info.imageColorSpace  = format.colorSpace;
	info.imageExtent      = swapchain_size;
	info.imageArrayLayers = 1;
	info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform     = pre_transform;
	info.compositeAlpha   = composite;
	info.presentMode      = swapchain_present_mode;
	info.clipped          = true;
	info.oldSwapchain     = old_swapchain;

	VK_CHECK(vkCreateSwapchainKHR(context.device, &info, nullptr, &context.swapchain));

	if (old_swapchain != VK_NULL_HANDLE)
	{
		for (auto &iv : context.swapchain_image_views)
		{
			vkDestroyImageView(context.device, iv, nullptr);
		}
		for (auto &pf : context.per_frame)
		{
			teardown_per_frame(pf);
		}
		context.swapchain_image_views.clear();
		vkDestroySwapchainKHR(context.device, old_swapchain, nullptr);
	}

	context.swapchain_dim = {swapchain_size.width, swapchain_size.height, format.format};

	uint32_t image_count;
	VK_CHECK(vkGetSwapchainImagesKHR(context.device, context.swapchain, &image_count, nullptr));
	std::vector<VkImage> swapchain_images(image_count);
	VK_CHECK(vkGetSwapchainImagesKHR(context.device, context.swapchain, &image_count, swapchain_images.data()));

	LOGI("Swapchain created: {}x{}, {} images",
	     swapchain_size.width, swapchain_size.height, image_count);

	context.per_frame.clear();
	context.per_frame.resize(image_count);
	for (size_t i = 0; i < image_count; i++)
	{
		init_per_frame(context.per_frame[i]);
	}

	context.swapchain_image_views.clear();
	for (size_t i = 0; i < image_count; i++)
	{
		VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view_info.image                       = swapchain_images[i];
		view_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format                      = context.swapchain_dim.format;
		view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.baseMipLevel   = 0;
		view_info.subresourceRange.levelCount     = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount     = 1;

		VkImageView image_view;
		VK_CHECK(vkCreateImageView(context.device, &view_info, nullptr, &image_view));
		context.swapchain_image_views.push_back(image_view);
	}
}

void OHOSTriangle::init_render_pass()
{
	// Create a "compatibility" RenderPass via framework for Pipeline creation.
	// The framework's set_attachment_layouts() sets both initialLayout and
	// finalLayout to the color attachment reference layout.  We can't make
	// finalLayout = PRESENT_SRC_KHR without also making the subpass reference
	// layout PRESENT_SRC_KHR (which is wrong for rendering).
	// Solution: use framework RenderPass for pipeline compatibility, and a
	// separate manual VkRenderPass for actual rendering (finalLayout=PRESENT_SRC_KHR).
	vkb::rendering::AttachmentC color_attachment{};
	color_attachment.format  = context.swapchain_dim.format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	std::vector<vkb::rendering::AttachmentC> attachments = {color_attachment};
	std::vector<vkb::LoadStoreInfo> load_store = {{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE}};

	// Provide SubpassInfo so the framework uses COLOR_ATTACHMENT_OPTIMAL for the
	// color attachment reference (matching our manual VkRenderPass for compatibility).
	vkb::SubpassInfo subpass_info{};
	subpass_info.output_attachments = {0};
	std::vector<vkb::SubpassInfo> subpasses = {subpass_info};

	fw_render_pass = &fw_device->get_resource_cache().request_render_pass(attachments, load_store, subpasses);
	OHOS_LOGI("init_render_pass: framework compatibility RenderPass (cached)");

	// Manual render pass with finalLayout = PRESENT_SRC_KHR for actual rendering.
	VkAttachmentDescription attachment = {};
	attachment.format         = context.swapchain_dim.format;
	attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments    = &color_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass   = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	auto rp_info              = vkb::initializers::render_pass_create_info();
	rp_info.attachmentCount = 1;
	rp_info.pAttachments    = &attachment;
	rp_info.subpassCount    = 1;
	rp_info.pSubpasses      = &subpass;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies   = &dependency;

	VK_CHECK(vkCreateRenderPass(context.device, &rp_info, nullptr, &context.render_pass));
	OHOS_LOGI("init_render_pass: manual VkRenderPass created (finalLayout=PRESENT_SRC_KHR)");
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
	context.framebuffers.clear();
	for (size_t i = 0; i < context.swapchain_image_views.size(); i++)
	{
		auto fb_info          = vkb::initializers::framebuffer_create_info();
		fb_info.renderPass      = context.render_pass;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments    = &context.swapchain_image_views[i];
		fb_info.width           = context.swapchain_dim.width;
		fb_info.height          = context.swapchain_dim.height;
		fb_info.layers          = 1;

		VkFramebuffer fb;
		VK_CHECK(vkCreateFramebuffer(context.device, &fb_info, nullptr, &fb));
		context.framebuffers.push_back(fb);
	}
}

VkResult OHOSTriangle::acquire_next_image(uint32_t *image)
{
	VkSemaphore acquire_semaphore = fw_semaphore_pool->request_semaphore();

	VkResult res = vkAcquireNextImageKHR(context.device, context.swapchain, UINT64_MAX, acquire_semaphore, VK_NULL_HANDLE, image);
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

	if (context.per_frame[*image].primary_command_pool != VK_NULL_HANDLE)
	{
		vkResetCommandPool(context.device, context.per_frame[*image].primary_command_pool, 0);
	}

	// Store acquire semaphore as the wait semaphore for this frame
	// (previously stored in swapchain_acquire_semaphore, now reuse render_complete_sem slot)
	context.per_frame[*image].render_complete_sem = acquire_semaphore;

	return VK_SUCCESS;
}

void OHOSTriangle::render_triangle(uint32_t swapchain_index)
{
	VkFramebuffer framebuffer = context.framebuffers[swapchain_index];
	VkCommandBuffer cmd       = context.per_frame[swapchain_index].primary_command_buffer;

	auto begin_info = vkb::initializers::command_buffer_begin_info();
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

	auto rp_begin         = vkb::initializers::render_pass_begin_info();
	rp_begin.renderPass  = context.render_pass;
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
	submit_info.pCommandBuffers      = &context.per_frame[index].primary_command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = &render_done_sem;

	VK_CHECK(vkQueueSubmit(context.queue, 1, &submit_info, context.per_frame[index].queue_submit_fence));

	VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores    = &render_done_sem;
	present.swapchainCount     = 1;
	present.pSwapchains        = &context.swapchain;
	present.pImageIndices      = &index;

	return vkQueuePresentKHR(context.queue, &present);
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

	for (auto &fb : context.framebuffers)
	{
		vkDestroyFramebuffer(context.device, fb, nullptr);
	}
	// Pipeline and PipelineLayout owned by fw_pipeline / fw_pipeline_layout
	// RenderPass owned by fw_render_pass
	if (context.render_pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(context.device, context.render_pass, nullptr);
	}
	for (auto &iv : context.swapchain_image_views)
	{
		vkDestroyImageView(context.device, iv, nullptr);
	}
	if (context.swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);
	}
	for (auto &pf : context.per_frame)
	{
		teardown_per_frame(pf);
	}
	// Vertex buffer managed by fw_vertex_buffer (framework Buffer)
	fw_vertex_buffer.reset();
	vkb::allocated::shutdown();
	// VkDevice destroyed by fw_device destructor when enabled;
	// otherwise manually destroyed below.
	if (!fw_device && context.device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(context.device, nullptr);
		context.device = VK_NULL_HANDLE;
	}
	if (context.surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(context.instance, context.surface, nullptr);
	}
#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	if (context.debug_messenger != VK_NULL_HANDLE)
	{
		vkDestroyDebugUtilsMessengerEXT(context.instance, context.debug_messenger, nullptr);
	}
#endif
	// VkInstance destroyed by fw_instance destructor when enabled;
	// otherwise manually destroyed below.
	if (!fw_instance && context.instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(context.instance, nullptr);
		context.instance = VK_NULL_HANDLE;
	}
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

	// Step-by-step enabling framework wrappers to locate OUT_OF_HOST_MEMORY.
	fw_instance = std::make_unique<vkb::core::InstanceCpp>(
	    static_cast<vk::Instance>(context.instance),
	    std::vector<char const *>{},
	    false);
	OHOS_LOGI("prepare: fw_instance OK");

	fw_gpu = std::make_unique<vkb::core::PhysicalDeviceCpp>(
	    *fw_instance,
	    static_cast<vk::PhysicalDevice>(context.gpu));
	OHOS_LOGI("prepare: fw_gpu OK");

	fw_device = std::make_unique<vkb::core::DeviceC>(
	    reinterpret_cast<vkb::core::PhysicalDeviceC &>(*fw_gpu),
	    context.device,
	    context.surface);
	OHOS_LOGI("prepare: fw_device OK");

	// Create sync primitive pools
	fw_fence_pool     = std::make_unique<vkb::FencePool>(*fw_device);
	fw_semaphore_pool = std::make_unique<vkb::SemaphorePool>(*fw_device);

	// Initialize framework's global VMA allocator (required by Buffer/Image classes)
	vkb::allocated::init(*fw_device);

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
		vkQueueWaitIdle(context.queue);
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

	for (auto &fb : context.framebuffers)
	{
		vkDestroyFramebuffer(context.device, fb, nullptr);
	}
	context.framebuffers.clear();

	// Reset framework pipeline objects — clear cache instead of individual resets
	fw_device->get_resource_cache().clear_pipelines();
	fw_pipeline        = nullptr;
	fw_pipeline_layout = nullptr;
	fw_render_pass     = nullptr;
	fw_vert_shader     = nullptr;
	fw_frag_shader     = nullptr;
	// Destroy manual VkRenderPass (not managed by framework)
	if (context.render_pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(context.device, context.render_pass, nullptr);
	}
	context.pipeline        = VK_NULL_HANDLE;
	context.pipeline_layout = VK_NULL_HANDLE;
	context.render_pass     = VK_NULL_HANDLE;

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
