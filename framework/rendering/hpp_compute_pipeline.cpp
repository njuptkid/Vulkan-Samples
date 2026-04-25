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

#include "hpp_compute_pipeline.h"

namespace vkb
{

// ---------------------------------------------------------------------------
// HPPComputePass
// ---------------------------------------------------------------------------

HPPComputePass::HPPComputePass(vkb::rendering::RenderContextCpp &render_context,
                               vkb::core::HPPShaderSource      &&cs_source) :
    render_context{render_context},
    cs_source{std::move(cs_source)}
{}

HPPComputePass &HPPComputePass::bind_buffer(const std::string &name,
                                             vkb::core::BufferCpp &buffer,
                                             vk::DeviceSize        offset,
                                             vk::DeviceSize        range)
{
	auto it = binding_index.find(name);
	if (it != binding_index.end())
	{
		ordered_buffer_bindings[it->second].second = {&buffer, offset, range};
	}
	else
	{
		binding_index[name] = ordered_buffer_bindings.size();
		ordered_buffer_bindings.push_back({name, {&buffer, offset, range}});
	}
	return *this;
}

HPPComputePass &HPPComputePass::set_dispatch_size(glm::uvec3 size)
{
	n_workgroups = size;
	return *this;
}

HPPComputePass &HPPComputePass::set_dispatch_size(uint32_t x, uint32_t y, uint32_t z)
{
	n_workgroups = {x, y, z};
	return *this;
}

void HPPComputePass::draw(vkb::core::CommandBufferCpp &command_buffer)
{
	auto &resource_cache = render_context.get_device().get_resource_cache();

	// Request shader module and pipeline layout from resource cache
	auto &shader_module = resource_cache.request_shader_module(
	    vk::ShaderStageFlagBits::eCompute, cs_source);

	auto &pipeline_layout = resource_cache.request_pipeline_layout({&shader_module});
	cached_pipeline_layout = &pipeline_layout;

	command_buffer.bind_pipeline_layout(pipeline_layout);

	// Bind buffers by name using shader reflection
	const auto &bindings = pipeline_layout.get_descriptor_set_layout(0);

	for (const auto &[name, binding] : ordered_buffer_bindings)
	{
		if (auto layout_binding = bindings.get_layout_binding(name))
		{
			command_buffer.bind_buffer(*binding.buffer,
			                           binding.offset,
			                           binding.range,
			                           0,
			                           layout_binding->binding,
			                           0);
		}
	}

	// Apply specialization constants
	for (const auto &[constant_id, data] : specialization_constants)
	{
		command_buffer.set_specialization_constant(constant_id, data);
	}

	// Push constants
	if (!push_constants_data.empty())
	{
		command_buffer.push_constants(push_constants_data);
	}

	// Dispatch
	command_buffer.dispatch(n_workgroups.x, n_workgroups.y, n_workgroups.z);
}

// ---------------------------------------------------------------------------
// HPPComputePipeline
// ---------------------------------------------------------------------------

HPPComputePipeline::HPPComputePipeline(vkb::rendering::RenderContextCpp &render_context) :
    render_context{render_context}
{}

HPPComputePass &HPPComputePipeline::add_pass(vkb::core::HPPShaderSource &&cs_source)
{
	passes.emplace_back(std::make_unique<HPPComputePass>(render_context, std::move(cs_source)));
	return *passes.back();
}

void HPPComputePipeline::dispatch(vkb::core::CommandBufferCpp &command_buffer)
{
	for (size_t i = 0; i < passes.size(); ++i)
	{
		passes[i]->draw(command_buffer);

		// Insert buffer barrier between passes 
		if (i + 1 < passes.size())
		{
			const auto &prev_bindings = passes[i]->get_buffer_bindings();

			for (const auto &[name, binding] : prev_bindings)
			{
				vkb::common::HPPBufferMemoryBarrier barrier{};
				barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
				barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
				barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
				barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

				command_buffer.buffer_memory_barrier(
				    *binding.buffer, binding.offset,
				    (binding.range == VK_WHOLE_SIZE) ? binding.buffer->get_size() : binding.range,
				    barrier);
			}
		}
	}
}

void HPPComputePipeline::barrier_to_vertex_input(vkb::core::CommandBufferCpp &command_buffer,
                                                   vkb::core::BufferCpp        &buffer,
                                                   vk::DeviceSize               offset,
                                                   vk::DeviceSize               size)
{
	vkb::common::HPPBufferMemoryBarrier barrier{};
	barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
	barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexInput;
	barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
	barrier.dst_access_mask = vk::AccessFlagBits::eVertexAttributeRead;

	command_buffer.buffer_memory_barrier(
	    buffer, offset,
	    (size == VK_WHOLE_SIZE) ? buffer.get_size() : size,
	    barrier);
}

void HPPComputePipeline::barrier_to_fragment_shader(vkb::core::CommandBufferCpp &command_buffer,
                                                      vkb::core::BufferCpp        &buffer,
                                                      vk::DeviceSize               offset,
                                                      vk::DeviceSize               size)
{
	vkb::common::HPPBufferMemoryBarrier barrier{};
	barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
	barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eFragmentShader;
	barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
	barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;

	command_buffer.buffer_memory_barrier(
	    buffer, offset,
	    (size == VK_WHOLE_SIZE) ? buffer.get_size() : size,
	    barrier);
}

}        // namespace vkb
