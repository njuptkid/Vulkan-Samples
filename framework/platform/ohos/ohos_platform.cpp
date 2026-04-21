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

#include "ohos_platform.h"
#include "platform/ohos/ohos_window.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace vkb
{
OHOSPlatform::OHOSPlatform(OHNativeWindow *native_window) :
    native_window{native_window}
{
}

OHOSPlatform::~OHOSPlatform()
{
	window.reset();
}

void OHOSPlatform::initialize()
{
	auto sink   = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto logger = std::make_shared<spdlog::logger>("logger", sink);
	logger->set_level(spdlog::level::info);
	logger->set_pattern("[%^%l%$] %v");
	spdlog::set_default_logger(logger);
	LOGI("OHOS platform initialized");
}

void OHOSPlatform::create_window(uint32_t width, uint32_t height)
{
	Window::Properties props;
	props.extent.width  = width;
	props.extent.height = height;
	props.mode          = Window::Mode::Fullscreen;
	props.resizable     = true;
	// OHOSWindow doesn't use the platform pointer for surface creation,
	// but the constructor requires it. Pass nullptr — OHOSWindow only
	// uses the OHNativeWindow handle for Vulkan surface creation.
	window = std::make_unique<OHOSWindow>(nullptr, native_window, props);
}

Window *OHOSPlatform::get_window()
{
	return window.get();
}
}        // namespace vkb
