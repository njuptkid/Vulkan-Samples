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

#include <native_window/external_window.h>
#include "platform/window.h"

namespace vkb
{
class OHOSPlatform;

class OHOSWindow : public Window
{
  public:
	OHOSWindow(OHOSPlatform *platform, OHNativeWindow *window, const Window::Properties &properties);

	virtual ~OHOSWindow() = default;

	virtual VkSurfaceKHR create_surface(vkb::core::InstanceC &instance) override;
	virtual VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice physical_device) override;

	virtual bool should_close() override;

	virtual void close() override;

	virtual float get_dpi_factor() const override;

	virtual float get_content_scale_factor() const override;

	virtual uint32_t get_display_index() const;

	virtual std::vector<const char *> get_required_surface_extensions() const override;

  private:
	OHOSPlatform   *platform{nullptr};
	OHNativeWindow *handle{nullptr};
};
}        // namespace vkb
