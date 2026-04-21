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
#include <core/platform/context.hpp>
#include <string>
#include <memory>
#include <vector>

namespace vkb
{
class Window;

/**
 * @brief Lightweight OHOS platform context for the NAPI bridge.
 *
 * Does NOT inherit from the heavy Platform base class to avoid pulling
 * in rendering/scene_graph/imgui dependencies. Instead, provides just
 * the minimal interface needed by the NAPI bridge to create a window
 * and drive the application lifecycle.
 */
class OHOSPlatformContext : public PlatformContext
{
  public:
	OHOSPlatformContext(OHNativeWindow *window,
	                    const std::string &storage_dir = "/data/storage/el2/base/haps/entry/files/",
	                    const std::string &temp_dir    = "/data/storage/el2/base/haps/entry/temp/") :
	    native_window{window}
	{
		_external_storage_directory = storage_dir;
		_temp_directory             = temp_dir;
	}

	OHNativeWindow *native_window{nullptr};
};

/**
 * @brief Lightweight OHOS platform for the NAPI bridge.
 *
 * Standalone class that creates an OHOSWindow and provides it to the
 * vkb::Application. Does NOT inherit from Platform base class.
 */
class OHOSPlatform
{
  public:
	OHOSPlatform(OHNativeWindow *native_window);

	~OHOSPlatform();

	/** Initialize logging (spdlog). */
	void initialize();

	/** Create the OHOS window. */
	void create_window(uint32_t width, uint32_t height);

	/** Get the window for passing to Application::prepare(). */
	Window *get_window();

  private:
	OHNativeWindow              *native_window{nullptr};
	std::unique_ptr<Window>      window;
};
}        // namespace vkb
