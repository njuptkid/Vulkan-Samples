/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License");
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

#include "common/hpp_vk_common.h"
#include "core/hpp_shader_module.h"
#include "rendering/render_context.h"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vkb
{

class HPPComputePass
{
  public:
	struct BufferBinding
	{
		vkb::core::BufferCpp *buffer;
		vk::DeviceSize        offset;
		vk::DeviceSize        range;
	};

	HPPComputePass(vkb::rendering::RenderContextCpp &render_context,
	               vkb::core::HPPShaderSource      &&cs_source);

	HPPComputePass &bind_buffer(const std::string &name,
	                            vkb::core::BufferCpp &buffer,
	                            vk::DeviceSize        offset = 0,
	                            vk::DeviceSize        range  = VK_WHOLE_SIZE);

	HPPComputePass &set_dispatch_size(glm::uvec3 size);
	HPPComputePass &set_dispatch_size(uint32_t x, uint32_t y, uint32_t z);

	template <typename T>
	HPPComputePass &set_push_constants(const T &data)
	{
		push_constants_data.resize(sizeof(T));
		std::memcpy(push_constants_data.data(), &data, sizeof(T));
		return *this;
	}

	void draw(vkb::core::CommandBufferCpp &command_buffer);

	const std::vector<std::pair<std::string, BufferBinding>> &get_buffer_bindings() const
	{
		return ordered_buffer_bindings;
	}

  private:
	vkb::rendering::RenderContextCpp               &render_context;
	vkb::core::HPPShaderSource                      cs_source;
	glm::uvec3                                      n_workgroups{1, 1, 1};
	std::unordered_map<std::string, size_t>         binding_index;
	std::vector<std::pair<std::string, BufferBinding>> ordered_buffer_bindings;
	std::map<uint32_t, std::vector<uint8_t>>        specialization_constants;
	std::vector<uint8_t>                            push_constants_data;
	vkb::core::HPPPipelineLayout                   *cached_pipeline_layout = nullptr;
};

class HPPComputePipeline
{
  public:
	HPPComputePipeline(vkb::rendering::RenderContextCpp &render_context);

	HPPComputePass &add_pass(vkb::core::HPPShaderSource &&cs_source);

	void dispatch(vkb::core::CommandBufferCpp &command_buffer);

	static void barrier_to_vertex_input(vkb::core::CommandBufferCpp &command_buffer,
	                                    vkb::core::BufferCpp        &buffer,
	                                    vk::DeviceSize               offset = 0,
	                                    vk::DeviceSize               size   = VK_WHOLE_SIZE);

	static void barrier_to_fragment_shader(vkb::core::CommandBufferCpp &command_buffer,
	                                       vkb::core::BufferCpp        &buffer,
	                                       vk::DeviceSize               offset = 0,
	                                       vk::DeviceSize               size   = VK_WHOLE_SIZE);

  private:
	vkb::rendering::RenderContextCpp                     &render_context;
	std::vector<std::unique_ptr<HPPComputePass>>          passes;
};

}        // namespace vkb
