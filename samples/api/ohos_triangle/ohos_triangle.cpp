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

#include <stdexcept>
#include <cstring>
#include <hilog/log.h>

#define LOG_TAG "OHOSTriangle"
#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO,  0xFF00, LOG_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, LOG_TAG, __VA_ARGS__)

#define VK_CHECK(x)                                                              \
	do {                                                                         \
		VkResult _res = (x);                                                     \
		if (_res != VK_SUCCESS) {                                                \
			LOGE("Vulkan error %{public}d at %{public}s:%{public}d", _res, __FILE__, __LINE__); \
			throw std::runtime_error("Vulkan error");                            \
		}                                                                        \
	} while (0)

// ---------------------------------------------------------------------------
// Embedded SPIR-V shaders (compiled from GLSL)
// ---------------------------------------------------------------------------

// Vertex shader (compiled from triangle.vert with vec3 inPos):
//   layout(location=0) in vec3 inPos;
//   layout(location=1) in vec3 inColor;
//   layout(location=0) out vec3 outColor;
//   void main() { outColor = inColor; gl_Position = vec4(inPos, 1.0); }
static const uint32_t k_vert_spirv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000001f, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0009000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00000013,
    0x00000016, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47,
    0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f,
    0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45,
    0x64756c63, 0x69645f65, 0x74636572, 0x00657669, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x4374756f, 0x726f6c6f,
    0x00000000, 0x00040005, 0x0000000b, 0x6f436e69, 0x00726f6c, 0x00060005,
    0x00000011, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x00000011, 0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006,
    0x00000011, 0x00000001, 0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000,
    0x00070006, 0x00000011, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369,
    0x0065636e, 0x00070006, 0x00000011, 0x00000003, 0x435f6c67, 0x446c6c75,
    0x61747369, 0x0065636e, 0x00030005, 0x00000013, 0x00000000, 0x00040005,
    0x00000016, 0x6f506e69, 0x00000073, 0x00040047, 0x00000009, 0x0000001e,
    0x00000000, 0x00040047, 0x0000000b, 0x0000001e, 0x00000001, 0x00030047,
    0x00000011, 0x00000002, 0x00050048, 0x00000011, 0x00000000, 0x0000000b,
    0x00000000, 0x00050048, 0x00000011, 0x00000001, 0x0000000b, 0x00000001,
    0x00050048, 0x00000011, 0x00000002, 0x0000000b, 0x00000003, 0x00050048,
    0x00000011, 0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x00000016,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000003, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040020, 0x0000000a,
    0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001,
    0x00040017, 0x0000000d, 0x00000006, 0x00000004, 0x00040015, 0x0000000e,
    0x00000020, 0x00000000, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000001,
    0x0004001c, 0x00000010, 0x00000006, 0x0000000f, 0x0006001e, 0x00000011,
    0x0000000d, 0x00000006, 0x00000010, 0x00000010, 0x00040020, 0x00000012,
    0x00000003, 0x00000011, 0x0004003b, 0x00000012, 0x00000013, 0x00000003,
    0x00040015, 0x00000014, 0x00000020, 0x00000001, 0x0004002b, 0x00000014,
    0x00000015, 0x00000000, 0x0004003b, 0x0000000a, 0x00000016, 0x00000001,
    0x0004002b, 0x00000006, 0x00000018, 0x3f800000, 0x00040020, 0x0000001d,
    0x00000003, 0x0000000d, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
    0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x0004003d, 0x00000007,
    0x00000017, 0x00000016, 0x00050051, 0x00000006, 0x00000019, 0x00000017,
    0x00000000, 0x00050051, 0x00000006, 0x0000001a, 0x00000017, 0x00000001,
    0x00050051, 0x00000006, 0x0000001b, 0x00000017, 0x00000002, 0x00070050,
    0x0000000d, 0x0000001c, 0x00000019, 0x0000001a, 0x0000001b, 0x00000018,
    0x00050041, 0x0000001d, 0x0000001e, 0x00000013, 0x00000015, 0x0003003e,
    0x0000001e, 0x0000001c, 0x000100fd, 0x00010038
};

// Fragment shader (compiled from triangle.frag):
//   layout(location=0) in vec3 inColor;
//   layout(location=0) out vec4 outFragColor;
//   void main() { outFragColor = vec4(inColor, 1.0); }
static const uint32_t k_frag_spirv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000013, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004,
    0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c,
    0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f,
    0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00060005, 0x00000009, 0x4674756f,
    0x43676172, 0x726f6c6f, 0x00000000, 0x00040005, 0x0000000c, 0x6f436e69,
    0x00726f6c, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000c, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040017,
    0x0000000a, 0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001,
    0x0000000a, 0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x0004002b,
    0x00000006, 0x0000000e, 0x3f800000, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a,
    0x0000000d, 0x0000000c, 0x00050051, 0x00000006, 0x0000000f, 0x0000000d,
    0x00000000, 0x00050051, 0x00000006, 0x00000010, 0x0000000d, 0x00000001,
    0x00050051, 0x00000006, 0x00000011, 0x0000000d, 0x00000002, 0x00070050,
    0x00000007, 0x00000012, 0x0000000f, 0x00000010, 0x00000011, 0x0000000e,
    0x0003003e, 0x00000009, 0x00000012, 0x000100fd, 0x00010038
};

// ---------------------------------------------------------------------------
// Static SPIR-V accessors
// ---------------------------------------------------------------------------

const std::vector<uint32_t> &OHOSTriangle::get_vert_spirv()
{
	static const std::vector<uint32_t> s(std::begin(k_vert_spirv), std::end(k_vert_spirv));
	return s;
}

const std::vector<uint32_t> &OHOSTriangle::get_frag_spirv()
{
	static const std::vector<uint32_t> s(std::begin(k_frag_spirv), std::end(k_frag_spirv));
	return s;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool OHOSTriangle::init(OHNativeWindow *native_window, uint32_t width, uint32_t height)
{
	if (initialized_)
	{
		return true;
	}

	try
	{
		ctx_.swapchain_dim.width  = width;
		ctx_.swapchain_dim.height = height;

		LOGI("init: step 1/8 - creating instance...");
		init_instance();
		LOGI("init: step 1/8 - instance OK.");

		LOGI("init: step 2/8 - creating device...");
		init_device();
		LOGI("init: step 2/8 - device OK.");

		LOGI("init: step 3/8 - creating surface (native_window=%{public}p)...", (void *)native_window);
		init_surface(native_window);
		LOGI("init: step 3/8 - surface OK.");

		LOGI("init: step 4/8 - creating swapchain...");
		init_swapchain();
		LOGI("init: step 4/8 - swapchain OK.");

		LOGI("init: step 5/8 - creating render pass...");
		init_render_pass();
		LOGI("init: step 5/8 - render pass OK.");

		LOGI("init: step 6/8 - creating framebuffers...");
		init_framebuffers();
		LOGI("init: step 6/8 - framebuffers OK.");

		LOGI("init: step 7/8 - creating vertex buffer...");
		init_vertex_buffer();
		LOGI("init: step 7/8 - vertex buffer OK.");

		LOGI("init: step 8/8 - creating pipeline...");
		init_pipeline();
		LOGI("init: step 8/8 - pipeline OK.");

		initialized_ = true;
		LOGI("OHOSTriangle initialized successfully.");
	}
	catch (const std::exception &e)
	{
		LOGE("OHOSTriangle init failed: %{public}s", e.what());
		cleanup();
		return false;
	}

	return true;
}

void OHOSTriangle::render()
{
	if (!initialized_)
	{
		return;
	}

	uint32_t image_index = 0;
	VkResult res         = acquire_next_image(&image_index);

	if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		// Swapchain needs recreation — skip this frame
		return;
	}
	if (res != VK_SUCCESS)
	{
		LOGE("Failed to acquire swapchain image: %d", res);
		return;
	}

	PerFrame &frame = ctx_.per_frame[image_index];

	// Wait for this frame's previous submission to finish
	VK_CHECK(vkWaitForFences(ctx_.device, 1, &frame.queue_submit_fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(ctx_.device, 1, &frame.queue_submit_fence));

	VK_CHECK(vkResetCommandPool(ctx_.device, frame.primary_command_pool, 0));

	record_command_buffer(frame.primary_command_buffer, image_index);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit_info{};
	submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount   = 1;
	submit_info.pWaitSemaphores      = &frame.swapchain_acquire_semaphore;
	submit_info.pWaitDstStageMask    = &wait_stage;
	submit_info.commandBufferCount   = 1;
	submit_info.pCommandBuffers      = &frame.primary_command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = &frame.swapchain_release_semaphore;

	VK_CHECK(vkQueueSubmit(ctx_.queue, 1, &submit_info, frame.queue_submit_fence));

	present_image(image_index);

	static uint32_t frame_count = 0;
	if (++frame_count % 300 == 0)
	{
		LOGI("Rendered 300 frames...");
	}
}

void OHOSTriangle::resize(uint32_t width, uint32_t height)
{
	if (!initialized_)
	{
		return;
	}

	vkDeviceWaitIdle(ctx_.device);

	teardown_framebuffers();

	ctx_.swapchain_dim.width  = width;
	ctx_.swapchain_dim.height = height;

	init_swapchain();
	init_framebuffers();
}

void OHOSTriangle::cleanup()
{
	if (ctx_.device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(ctx_.device);
	}

	teardown_framebuffers();

	for (auto &frame : ctx_.per_frame)
	{
		teardown_per_frame(frame);
	}
	ctx_.per_frame.clear();

	for (auto sem : ctx_.recycled_semaphores)
	{
		vkDestroySemaphore(ctx_.device, sem, nullptr);
	}
	ctx_.recycled_semaphores.clear();

	if (ctx_.vertex_buffer_memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(ctx_.device, ctx_.vertex_buffer_memory, nullptr);
		ctx_.vertex_buffer_memory = VK_NULL_HANDLE;
	}
	if (ctx_.vertex_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(ctx_.device, ctx_.vertex_buffer, nullptr);
		ctx_.vertex_buffer = VK_NULL_HANDLE;
	}

	if (ctx_.pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx_.device, ctx_.pipeline, nullptr);
		ctx_.pipeline = VK_NULL_HANDLE;
	}
	if (ctx_.pipeline_layout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx_.device, ctx_.pipeline_layout, nullptr);
		ctx_.pipeline_layout = VK_NULL_HANDLE;
	}
	if (ctx_.render_pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(ctx_.device, ctx_.render_pass, nullptr);
		ctx_.render_pass = VK_NULL_HANDLE;
	}

	for (auto iv : ctx_.swapchain_image_views)
	{
		vkDestroyImageView(ctx_.device, iv, nullptr);
	}
	ctx_.swapchain_image_views.clear();

	if (ctx_.swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(ctx_.device, ctx_.swapchain, nullptr);
		ctx_.swapchain = VK_NULL_HANDLE;
	}
	if (ctx_.surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr);
		ctx_.surface = VK_NULL_HANDLE;
	}
	if (ctx_.device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(ctx_.device, nullptr);
		ctx_.device = VK_NULL_HANDLE;
	}

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	if (ctx_.debug_messenger != VK_NULL_HANDLE)
	{
		vkDestroyDebugUtilsMessengerEXT(ctx_.instance, ctx_.debug_messenger, nullptr);
		ctx_.debug_messenger = VK_NULL_HANDLE;
	}
#endif

	if (ctx_.instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(ctx_.instance, nullptr);
		ctx_.instance = VK_NULL_HANDLE;
	}

	initialized_ = false;
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------

void OHOSTriangle::init_instance()
{
	LOGI("Initializing Vulkan Instance...");
	if (volkInitialize() != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to initialize volk.");
	}

	std::vector<const char *> extensions = {
	    VK_KHR_SURFACE_EXTENSION_NAME,
	    VK_OHOS_SURFACE_EXTENSION_NAME,
	};

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	VkApplicationInfo app_info{};
	app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "OHOSTriangle";
	app_info.pEngineName      = "Vulkan-Samples";
	app_info.apiVersion       = VK_API_VERSION_1_1;

	VkInstanceCreateInfo create_info{};
	create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo        = &app_info;
	create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
	create_info.ppEnabledExtensionNames = extensions.data();

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	const char *validation_layer    = "VK_LAYER_KHRONOS_validation";
	create_info.enabledLayerCount   = 1;
	create_info.ppEnabledLayerNames = &validation_layer;

	VkDebugUtilsMessengerCreateInfoEXT dbg_info{};
	dbg_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	dbg_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
	                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
	dbg_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                           VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
	dbg_info.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
	                               VkDebugUtilsMessageTypeFlagsEXT,
	                               const VkDebugUtilsMessengerCallbackDataEXT *data,
	                               void *) -> VkBool32 {
		OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, LOG_TAG, "Validation: %{public}s", data->pMessage);
		return VK_FALSE;
	};
	create_info.pNext = &dbg_info;
#endif

	VK_CHECK(vkCreateInstance(&create_info, nullptr, &ctx_.instance));
	volkLoadInstance(ctx_.instance);
	LOGI("Vulkan Instance created: %{public}p", (void *)ctx_.instance);

	// vkCreateSurfaceOHOS is an OHOS platform extension not in the standard
	// volk dispatch table — load it manually.
	fp_vkCreateSurfaceOHOS = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
	    vkGetInstanceProcAddr(ctx_.instance, "vkCreateSurfaceOHOS"));
	if (!fp_vkCreateSurfaceOHOS)
	{
		throw std::runtime_error("Failed to load vkCreateSurfaceOHOS — is VK_OHOS_surface supported?");
	}

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(ctx_.instance, &dbg_info, nullptr, &ctx_.debug_messenger));
#endif
}

void OHOSTriangle::init_device()
{
	LOGI("Initializing Physics Device...");
	uint32_t gpu_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(ctx_.instance, &gpu_count, nullptr));
	if (gpu_count == 0)
	{
		throw std::runtime_error("No Vulkan-capable GPU found.");
	}

	std::vector<VkPhysicalDevice> gpus(gpu_count);
	VK_CHECK(vkEnumeratePhysicalDevices(ctx_.instance, &gpu_count, gpus.data()));
	ctx_.gpu = gpus[0];

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(ctx_.gpu, &props);
	LOGI("Selected GPU: %{public}s", props.deviceName);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(ctx_.gpu, &queue_family_count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(ctx_.gpu, &queue_family_count, queue_families.data());

	// Find a graphics queue; presentation support is confirmed after surface creation
	ctx_.queue_index = -1;
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			ctx_.queue_index = static_cast<int32_t>(i);
			break;
		}
	}
	if (ctx_.queue_index < 0)
	{
		throw std::runtime_error("No graphics queue family found.");
	}

	float queue_priority = 1.0f;

	VkDeviceQueueCreateInfo queue_info{};
	queue_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = static_cast<uint32_t>(ctx_.queue_index);
	queue_info.queueCount       = 1;
	queue_info.pQueuePriorities = &queue_priority;

	const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	VkDeviceCreateInfo device_info{};
	device_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount    = 1;
	device_info.pQueueCreateInfos       = &queue_info;
	device_info.enabledExtensionCount   = 1;
	device_info.ppEnabledExtensionNames = device_extensions;

	VK_CHECK(vkCreateDevice(ctx_.gpu, &device_info, nullptr, &ctx_.device));
	volkLoadDevice(ctx_.device);

	vkGetDeviceQueue(ctx_.device, static_cast<uint32_t>(ctx_.queue_index), 0, &ctx_.queue);
}

void OHOSTriangle::init_surface(OHNativeWindow *native_window)
{
	LOGI("Creating Surface from OHNativeWindow: %{public}p", (void *)native_window);
	VkSurfaceCreateInfoOHOS surface_info{};
	surface_info.sType  = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
	surface_info.window = native_window;

	// Use the manually loaded function pointer (volk doesn't dispatch this)
	VK_CHECK(fp_vkCreateSurfaceOHOS(ctx_.instance, &surface_info, nullptr, &ctx_.surface));
	LOGI("Vulkan Surface created: %{public}p", (void *)ctx_.surface);

	// Verify the selected queue family supports presentation on this surface
	VkBool32 present_support = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(ctx_.gpu,
	                                              static_cast<uint32_t>(ctx_.queue_index),
	                                              ctx_.surface,
	                                              &present_support));
	if (!present_support)
	{
		throw std::runtime_error("Selected queue family does not support presentation.");
	}
}

void OHOSTriangle::init_swapchain()
{
	LOGI("Initializing Swapchain...");
	VkSurfaceCapabilitiesKHR caps{};
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_.gpu, ctx_.surface, &caps));

	uint32_t format_count = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.gpu, ctx_.surface, &format_count, nullptr));
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.gpu, ctx_.surface, &format_count, formats.data()));

	// Prefer RGBA8 SRGB; fall back to first available format
	VkSurfaceFormatKHR chosen = formats[0];
	for (const auto &f : formats)
	{
		if ((f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) &&
		    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosen = f;
			break;
		}
	}
	ctx_.swapchain_dim.format = chosen.format;

	VkExtent2D extent = caps.currentExtent;
	if (extent.width == 0xFFFFFFFF)
	{
		extent.width  = ctx_.swapchain_dim.width;
		extent.height = ctx_.swapchain_dim.height;
	}
	ctx_.swapchain_dim.width  = extent.width;
	ctx_.swapchain_dim.height = extent.height;

	uint32_t image_count = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
	{
		image_count = caps.maxImageCount;
	}

	VkSwapchainCreateInfoKHR sc_info{};
	sc_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sc_info.surface          = ctx_.surface;
	sc_info.minImageCount    = image_count;
	sc_info.imageFormat      = chosen.format;
	sc_info.imageColorSpace  = chosen.colorSpace;
	sc_info.imageExtent      = extent;
	sc_info.imageArrayLayers = 1;
	sc_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sc_info.preTransform     = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
	                               ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
	                               : caps.currentTransform;
	sc_info.compositeAlpha   = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	sc_info.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
	sc_info.clipped          = VK_TRUE;
	sc_info.oldSwapchain     = ctx_.swapchain;

	// Select composite alpha
	sc_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
	{
		if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
		{
			sc_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		}
		else
		{
			// Fallback to whatever is supported
			for (uint32_t i = 0; i < 32; i++)
			{
				VkCompositeAlphaFlagBitsKHR alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(1u << i);
				if (caps.supportedCompositeAlpha & alpha)
				{
					sc_info.compositeAlpha = alpha;
					break;
				}
			}
		}
	}
	LOGI("Selected Composite Alpha: %d", (int)sc_info.compositeAlpha);

	VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSwapchainKHR(ctx_.device, &sc_info, nullptr, &new_swapchain));
	LOGI("Swapchain created: %{public}d x %{public}d", extent.width, extent.height);

	// Destroy old swapchain and image views if this is a resize
	if (ctx_.swapchain != VK_NULL_HANDLE)
	{
		for (auto iv : ctx_.swapchain_image_views)
		{
			vkDestroyImageView(ctx_.device, iv, nullptr);
		}
		ctx_.swapchain_image_views.clear();

		for (auto &frame : ctx_.per_frame)
		{
			teardown_per_frame(frame);
		}
		ctx_.per_frame.clear();

		vkDestroySwapchainKHR(ctx_.device, ctx_.swapchain, nullptr);
	}
	ctx_.swapchain = new_swapchain;

	uint32_t actual_count = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(ctx_.device, ctx_.swapchain, &actual_count, nullptr));
	ctx_.swapchain_images.resize(actual_count);
	VK_CHECK(vkGetSwapchainImagesKHR(ctx_.device, ctx_.swapchain, &actual_count, ctx_.swapchain_images.data()));

	// Create image views
	ctx_.swapchain_image_views.resize(actual_count);
	for (uint32_t i = 0; i < actual_count; i++)
	{
		VkImageViewCreateInfo iv_info{};
		iv_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		iv_info.image                           = ctx_.swapchain_images[i];
		iv_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		iv_info.format                          = ctx_.swapchain_dim.format;
		iv_info.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
		iv_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		iv_info.subresourceRange.baseMipLevel   = 0;
		iv_info.subresourceRange.levelCount     = 1;
		iv_info.subresourceRange.baseArrayLayer = 0;
		iv_info.subresourceRange.layerCount     = 1;
		VK_CHECK(vkCreateImageView(ctx_.device, &iv_info, nullptr, &ctx_.swapchain_image_views[i]));
	}

	// Create per-frame resources
	ctx_.per_frame.resize(actual_count);
	for (auto &frame : ctx_.per_frame)
	{
		init_per_frame(frame);
	}
}

void OHOSTriangle::init_render_pass()
{
	VkAttachmentDescription color_attachment{};
	color_attachment.format         = ctx_.swapchain_dim.format;
	color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_ref{};
	color_ref.attachment = 0;
	color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments    = &color_ref;

	VkSubpassDependency dep{};
	dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass    = 0;
	dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.srcAccessMask = 0;
	dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo rp_info{};
	rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp_info.attachmentCount = 1;
	rp_info.pAttachments    = &color_attachment;
	rp_info.subpassCount    = 1;
	rp_info.pSubpasses      = &subpass;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies   = &dep;

	VK_CHECK(vkCreateRenderPass(ctx_.device, &rp_info, nullptr, &ctx_.render_pass));
}

void OHOSTriangle::init_framebuffers()
{
	ctx_.framebuffers.resize(ctx_.swapchain_image_views.size());
	for (size_t i = 0; i < ctx_.swapchain_image_views.size(); i++)
	{
		VkFramebufferCreateInfo fb_info{};
		fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass      = ctx_.render_pass;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments    = &ctx_.swapchain_image_views[i];
		fb_info.width           = ctx_.swapchain_dim.width;
		fb_info.height          = ctx_.swapchain_dim.height;
		fb_info.layers          = 1;
		VK_CHECK(vkCreateFramebuffer(ctx_.device, &fb_info, nullptr, &ctx_.framebuffers[i]));
	}
}

void OHOSTriangle::init_vertex_buffer()
{
	// Triangle vertices: NDC position + RGB color
	const Vertex vertices[] = {
	    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	    {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};
	VkDeviceSize buffer_size = sizeof(vertices);

	VkBufferCreateInfo buf_info{};
	buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buf_info.size  = buffer_size;
	buf_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VK_CHECK(vkCreateBuffer(ctx_.device, &buf_info, nullptr, &ctx_.vertex_buffer));

	VkMemoryRequirements mem_req{};
	vkGetBufferMemoryRequirements(ctx_.device, ctx_.vertex_buffer, &mem_req);

	VkMemoryAllocateInfo alloc_info{};
	alloc_info.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_req.size;
	alloc_info.memoryTypeIndex =
	    find_memory_type(mem_req.memoryTypeBits,
	                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VK_CHECK(vkAllocateMemory(ctx_.device, &alloc_info, nullptr, &ctx_.vertex_buffer_memory));
	VK_CHECK(vkBindBufferMemory(ctx_.device, ctx_.vertex_buffer, ctx_.vertex_buffer_memory, 0));

	void *data = nullptr;
	VK_CHECK(vkMapMemory(ctx_.device, ctx_.vertex_buffer_memory, 0, buffer_size, 0, &data));
	memcpy(data, vertices, static_cast<size_t>(buffer_size));
	vkUnmapMemory(ctx_.device, ctx_.vertex_buffer_memory);
}

void OHOSTriangle::init_pipeline()
{
	LOGI("init_pipeline: loading vert shader (%{public}zu bytes)...", sizeof(k_vert_spirv));
	VkShaderModule vert = load_shader_spirv({reinterpret_cast<const uint8_t *>(k_vert_spirv),
	                                         reinterpret_cast<const uint8_t *>(k_vert_spirv) + sizeof(k_vert_spirv)});
	LOGI("init_pipeline: vert shader module=%{public}p", (void *)vert);

	LOGI("init_pipeline: loading frag shader (%{public}zu bytes)...", sizeof(k_frag_spirv));
	VkShaderModule frag = load_shader_spirv({reinterpret_cast<const uint8_t *>(k_frag_spirv),
	                                         reinterpret_cast<const uint8_t *>(k_frag_spirv) + sizeof(k_frag_spirv)});
	LOGI("init_pipeline: frag shader module=%{public}p", (void *)frag);

	VkPipelineLayoutCreateInfo layout_info{};
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	VK_CHECK(vkCreatePipelineLayout(ctx_.device, &layout_info, nullptr, &ctx_.pipeline_layout));
	LOGI("init_pipeline: pipeline layout created.");

	VkPipelineShaderStageCreateInfo vert_stage{};
	vert_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
	vert_stage.module = vert;
	vert_stage.pName  = "main";

	VkPipelineShaderStageCreateInfo frag_stage{};
	frag_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_stage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_stage.module = frag;
	frag_stage.pName  = "main";

	VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

	// Vertex input: binding 0, per-vertex
	VkVertexInputBindingDescription binding{};
	binding.binding   = 0;
	binding.stride    = sizeof(Vertex);
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[2]{};
	attrs[0].location = 0;
	attrs[0].binding  = 0;
	attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;    // pos
	attrs[0].offset   = offsetof(Vertex, pos);
	attrs[1].location = 1;
	attrs[1].binding  = 0;
	attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;   // color
	attrs[1].offset   = offsetof(Vertex, color);

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount   = 1;
	vi.pVertexBindingDescriptions      = &binding;
	vi.vertexAttributeDescriptionCount = 2;
	vi.pVertexAttributeDescriptions    = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = {0, 0, (float)ctx_.swapchain_dim.width, (float)ctx_.swapchain_dim.height, 0, 1};
	VkRect2D   scissor  = {{0, 0}, {ctx_.swapchain_dim.width, ctx_.swapchain_dim.height}};

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.pViewports    = &viewport;
	vp.scissorCount  = 1;
	vp.pScissors     = &scissor;

	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.lineWidth = 1.0f;
	raster.cullMode  = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // Matches current triangle vertices order
	LOGI("Pipeline state: CullMode=NONE, FrontFace=CLOCKWISE");

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_att{};
	blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend{};
	blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments    = &blend_att;

	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dyn{};
	dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates    = dynamic_states;

	VkGraphicsPipelineCreateInfo gfx_info{};
	gfx_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gfx_info.stageCount          = 2;
	gfx_info.pStages             = stages;
	gfx_info.pVertexInputState   = &vi;
	gfx_info.pInputAssemblyState = &ia;
	gfx_info.pViewportState      = &vp;
	gfx_info.pRasterizationState = &raster;
	gfx_info.pMultisampleState   = &ms;
	gfx_info.pColorBlendState    = &blend;
	gfx_info.pDynamicState       = &dyn;
	gfx_info.layout              = ctx_.pipeline_layout;
	gfx_info.renderPass          = ctx_.render_pass;
	gfx_info.subpass             = 0;

	VK_CHECK(vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &gfx_info, nullptr, &ctx_.pipeline));
	LOGI("init_pipeline: graphics pipeline created.");

	vkDestroyShaderModule(ctx_.device, vert, nullptr);
	vkDestroyShaderModule(ctx_.device, frag, nullptr);
}

void OHOSTriangle::init_per_frame(PerFrame &frame)
{
	VkFenceCreateInfo fence_info{};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VK_CHECK(vkCreateFence(ctx_.device, &fence_info, nullptr, &frame.queue_submit_fence));

	VkCommandPoolCreateInfo pool_info{};
	pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool_info.queueFamilyIndex = static_cast<uint32_t>(ctx_.queue_index);
	VK_CHECK(vkCreateCommandPool(ctx_.device, &pool_info, nullptr, &frame.primary_command_pool));

	VkCommandBufferAllocateInfo cmd_info{};
	cmd_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_info.commandPool        = frame.primary_command_pool;
	cmd_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_info.commandBufferCount = 1;
	VK_CHECK(vkAllocateCommandBuffers(ctx_.device, &cmd_info, &frame.primary_command_buffer));

	VkSemaphoreCreateInfo sem_info{};
	sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VK_CHECK(vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &frame.swapchain_acquire_semaphore));
	VK_CHECK(vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &frame.swapchain_release_semaphore));
}

// ---------------------------------------------------------------------------
// Teardown helpers
// ---------------------------------------------------------------------------

void OHOSTriangle::teardown_per_frame(PerFrame &frame)
{
	if (frame.queue_submit_fence != VK_NULL_HANDLE)
	{
		vkDestroyFence(ctx_.device, frame.queue_submit_fence, nullptr);
		frame.queue_submit_fence = VK_NULL_HANDLE;
	}
	if (frame.primary_command_buffer != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(ctx_.device, frame.primary_command_pool, 1, &frame.primary_command_buffer);
		frame.primary_command_buffer = VK_NULL_HANDLE;
	}
	if (frame.primary_command_pool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(ctx_.device, frame.primary_command_pool, nullptr);
		frame.primary_command_pool = VK_NULL_HANDLE;
	}
	if (frame.swapchain_acquire_semaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(ctx_.device, frame.swapchain_acquire_semaphore, nullptr);
		frame.swapchain_acquire_semaphore = VK_NULL_HANDLE;
	}
	if (frame.swapchain_release_semaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(ctx_.device, frame.swapchain_release_semaphore, nullptr);
		frame.swapchain_release_semaphore = VK_NULL_HANDLE;
	}
}

void OHOSTriangle::teardown_framebuffers()
{
	for (auto fb : ctx_.framebuffers)
	{
		vkDestroyFramebuffer(ctx_.device, fb, nullptr);
	}
	ctx_.framebuffers.clear();
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

VkResult OHOSTriangle::acquire_next_image(uint32_t *image_index)
{
	VkSemaphore acquire_sem = VK_NULL_HANDLE;

	if (!ctx_.recycled_semaphores.empty())
	{
		acquire_sem = ctx_.recycled_semaphores.back();
		ctx_.recycled_semaphores.pop_back();
	}
	else
	{
		VkSemaphoreCreateInfo sem_info{};
		sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VK_CHECK(vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &acquire_sem));
	}

	VkResult res = vkAcquireNextImageKHR(ctx_.device, ctx_.swapchain, UINT64_MAX,
	                                     acquire_sem, VK_NULL_HANDLE, image_index);
	if (res != VK_SUCCESS)
	{
		ctx_.recycled_semaphores.push_back(acquire_sem);
		return res;
	}

	// Swap in the new semaphore; recycle the old one
	PerFrame &frame = ctx_.per_frame[*image_index];
	ctx_.recycled_semaphores.push_back(frame.swapchain_acquire_semaphore);
	frame.swapchain_acquire_semaphore = acquire_sem;

	return VK_SUCCESS;
}

void OHOSTriangle::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index)
{
	VkCommandBufferBeginInfo begin_info{};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

	VkClearValue clear_value{};
	clear_value.color = {{0.1f, 0.1f, 0.2f, 1.0f}};

	VkRenderPassBeginInfo rp_begin{};
	rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.renderPass        = ctx_.render_pass;
	rp_begin.framebuffer       = ctx_.framebuffers[image_index];
	rp_begin.renderArea.extent = {ctx_.swapchain_dim.width, ctx_.swapchain_dim.height};
	rp_begin.clearValueCount   = 1;
	rp_begin.pClearValues      = &clear_value;

	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx_.pipeline);

	VkViewport viewport = {0, 0, (float)ctx_.swapchain_dim.width, (float)ctx_.swapchain_dim.height, 0, 1};
	VkRect2D   scissor  = {{0, 0}, {ctx_.swapchain_dim.width, ctx_.swapchain_dim.height}};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &ctx_.vertex_buffer, &offset);
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));
}

VkResult OHOSTriangle::present_image(uint32_t image_index)
{
	PerFrame &frame = ctx_.per_frame[image_index];

	VkPresentInfoKHR present_info{};
	present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores    = &frame.swapchain_release_semaphore;
	present_info.swapchainCount     = 1;
	present_info.pSwapchains        = &ctx_.swapchain;
	present_info.pImageIndices      = &image_index;

	return vkQueuePresentKHR(ctx_.queue, &present_info);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

VkShaderModule OHOSTriangle::load_shader_spirv(const std::vector<uint8_t> &spirv)
{
	VkShaderModuleCreateInfo sm_info{};
	sm_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	sm_info.codeSize = spirv.size();
	sm_info.pCode    = reinterpret_cast<const uint32_t *>(spirv.data());

	VkShaderModule module = VK_NULL_HANDLE;
	VK_CHECK(vkCreateShaderModule(ctx_.device, &sm_info, nullptr, &module));
	return module;
}

uint32_t OHOSTriangle::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties mem_props{};
	vkGetPhysicalDeviceMemoryProperties(ctx_.gpu, &mem_props);

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((type_filter & (1u << i)) &&
		    (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	throw std::runtime_error("Failed to find suitable memory type.");
}
