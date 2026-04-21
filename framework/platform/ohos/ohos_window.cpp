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

#include "ohos_window.h"
#include "ohos_platform.h"
#include "common/error.h"

#include <vulkan/vulkan_ohos.h>

#ifndef VK_OHOS_SURFACE_EXTENSION_NAME
#	define VK_OHOS_SURFACE_EXTENSION_NAME "VK_OHOS_surface"
#endif

namespace vkb
{
// ---------------------------------------------------------------------------
// Window base class methods (inlined from window.cpp to avoid pulling in
// platform.h which has heavy dependencies on rendering/scene_graph/imgui)
// ---------------------------------------------------------------------------

Window::Window(const Properties &properties) :
    properties{properties}
{
}

void Window::process_events()
{
}

Window::Extent Window::resize(const Extent &new_extent)
{
	if (properties.resizable)
	{
		properties.extent.width  = new_extent.width;
		properties.extent.height = new_extent.height;
	}
	return properties.extent;
}

const Window::Extent &Window::get_extent() const
{
	return properties.extent;
}

float Window::get_content_scale_factor() const
{
	return 1.0f;
}

Window::Mode Window::get_window_mode() const
{
	return properties.mode;
}

bool Window::get_display_present_info(VkDisplayPresentInfoKHR *info,
                                      uint32_t src_width, uint32_t src_height) const
{
	return false;
}

// ---------------------------------------------------------------------------
// OHOSWindow implementation
// ---------------------------------------------------------------------------

OHOSWindow::OHOSWindow(OHOSPlatform *platform, OHNativeWindow *window, const Window::Properties &properties) :
    Window{properties},
    platform{platform},
    handle{window}
{
	if (handle)
	{
		// Configure the native window buffer geometry to match the expected extent.
		// OHOS Vulkan driver requires SET_BUFFER_GEOMETRY before swapchain creation;
		// without it, vkCreateSwapchainKHR fails with "Extent size is not unique".
		OH_NativeWindow_NativeWindowHandleOpt(handle, SET_BUFFER_GEOMETRY,
		                                      static_cast<int32_t>(this->properties.extent.width),
		                                      static_cast<int32_t>(this->properties.extent.height));
	}
}

VkSurfaceKHR OHOSWindow::create_surface(vkb::core::InstanceC &instance)
{
	return create_surface(instance.get_handle(), VK_NULL_HANDLE);
}

VkSurfaceKHR OHOSWindow::create_surface(VkInstance instance, VkPhysicalDevice physical_device)
{
	if (instance == VK_NULL_HANDLE || !handle)
	{
		return VK_NULL_HANDLE;
	}

	VkSurfaceKHR surface = VK_NULL_HANDLE;

	PFN_vkCreateSurfaceOHOS vkCreateSurfaceOHOS =
	    reinterpret_cast<PFN_vkCreateSurfaceOHOS>(vkGetInstanceProcAddr(instance, "vkCreateSurfaceOHOS"));

	if (!vkCreateSurfaceOHOS)
	{
		LOGE("Vulkan instance does not support VK_OHOS_surface extension");
		return VK_NULL_HANDLE;
	}

	VkSurfaceCreateInfoOHOS create_info{};
	create_info.sType  = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
	create_info.pNext  = nullptr;
	create_info.flags  = 0;
	create_info.window = handle;

	VK_CHECK(vkCreateSurfaceOHOS(instance, &create_info, nullptr, &surface));

	return surface;
}

bool OHOSWindow::should_close()
{
	return false;
}

void OHOSWindow::close()
{
}

float OHOSWindow::get_dpi_factor() const
{
	return 1.0f;
}

float OHOSWindow::get_content_scale_factor() const
{
	return 1.0f;
}

uint32_t OHOSWindow::get_display_index() const
{
	return 0;
}

std::vector<const char *> OHOSWindow::get_required_surface_extensions() const
{
	return {VK_OHOS_SURFACE_EXTENSION_NAME};
}

}        // namespace vkb
