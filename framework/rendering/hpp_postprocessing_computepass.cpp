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

#include "hpp_postprocessing_computepass.h"
#include "hpp_postprocessing_pipeline.h"
#include "common/hpp_vk_common.h"

namespace vkb
{
HPPPostProcessingComputePass::HPPPostProcessingComputePass(HPPPostProcessingPipeline *parent, const vkb::core::HPPShaderSource &cs_source, const vkb::core::HPPShaderVariant &cs_variant,
                                                           std::shared_ptr<core::HPPSampler> &&default_sampler) :
    HPPPostProcessingPass{parent},
    cs_source{cs_source},
    cs_variant{cs_variant},
    default_sampler{std::move(default_sampler)}
{
	if (this->default_sampler == nullptr)
	{
		vk::SamplerCreateInfo sampler_info;
		sampler_info.minFilter        = vk::Filter::eLinear;
		sampler_info.magFilter        = vk::Filter::eLinear;
		sampler_info.mipmapMode       = vk::SamplerMipmapMode::eNearest;
		sampler_info.addressModeU     = vk::SamplerAddressMode::eClampToEdge;
		sampler_info.addressModeV     = vk::SamplerAddressMode::eClampToEdge;
		sampler_info.addressModeW     = vk::SamplerAddressMode::eClampToEdge;
		sampler_info.mipLodBias       = 0.0f;
		sampler_info.compareOp        = vk::CompareOp::eNever;
		sampler_info.minLod           = 0.0f;
		sampler_info.maxLod           = 0.0f;
		sampler_info.anisotropyEnable = false;
		sampler_info.maxAnisotropy    = 0.0f;
		sampler_info.borderColor      = vk::BorderColor::eFloatOpaqueWhite;

		this->default_sampler = std::make_shared<vkb::core::HPPSampler>(get_render_context().get_device(), sampler_info);

		sampler_info.minFilter = vk::Filter::eNearest;
		sampler_info.magFilter = vk::Filter::eNearest;

		this->default_sampler_nearest = std::make_shared<vkb::core::HPPSampler>(get_render_context().get_device(), sampler_info);
	}
}

void HPPPostProcessingComputePass::prepare(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
{
	auto &resource_cache = get_render_context().get_device().get_resource_cache();
	resource_cache.request_shader_module(vk::ShaderStageFlagBits::eCompute, cs_source, cs_variant);
}

HPPPostProcessingComputePass &HPPPostProcessingComputePass::bind_sampled_image(const std::string &name, core::HPPSampledImage &&new_image)
{
	auto it = sampled_images.find(name);
	if (it != sampled_images.end())
	{
		it->second = std::move(new_image);
	}
	else
	{
		sampled_images.emplace(name, std::move(new_image));
	}
	return *this;
}

HPPPostProcessingComputePass &HPPPostProcessingComputePass::bind_storage_image(const std::string &name, core::HPPSampledImage &&new_image)
{
	auto it = storage_images.find(name);
	if (it != storage_images.end())
	{
		it->second = std::move(new_image);
	}
	else
	{
		storage_images.emplace(name, std::move(new_image));
	}
	return *this;
}

void HPPPostProcessingComputePass::transition_images(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
{
	BarrierInfo fallback_barrier_src{};
	fallback_barrier_src.pipeline_stage     = vk::PipelineStageFlagBits::eComputeShader;
	fallback_barrier_src.image_read_access  = {};
	fallback_barrier_src.image_write_access = {};
	const auto prev_pass_barrier_info       = get_predecessor_src_barrier_info(fallback_barrier_src);

	auto &resource_cache  = command_buffer.get_device().get_resource_cache();
	auto &shader_module   = resource_cache.request_shader_module(vk::ShaderStageFlagBits::eCompute, cs_source, cs_variant);
	auto &pipeline_layout = resource_cache.request_pipeline_layout({&shader_module});

	for (const auto &sampled : sampled_images)
	{
		if (const uint32_t *attachment = sampled.second.get_target_attachment())
		{
			auto *sampled_rt = sampled.second.get_render_target();
			if (sampled_rt == nullptr)
			{
				sampled_rt = &default_render_target;
			}

			if (sampled_rt->get_layout(*attachment) == vk::ImageLayout::eShaderReadOnlyOptimal)
			{
				continue;
			}

			vkb::common::HPPImageMemoryBarrier barrier;
			barrier.old_layout      = sampled_rt->get_layout(*attachment);
			barrier.new_layout      = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.src_access_mask = prev_pass_barrier_info.image_write_access;
			barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
			barrier.src_stage_mask  = prev_pass_barrier_info.pipeline_stage;
			barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;

			assert(*attachment < sampled_rt->get_views().size());
			command_buffer.image_memory_barrier(sampled_rt->get_views()[*attachment], barrier);
			sampled_rt->set_layout(*attachment, vk::ImageLayout::eShaderReadOnlyOptimal);
		}
	}

	for (const auto &storage : storage_images)
	{
		if (const uint32_t *attachment = storage.second.get_target_attachment())
		{
			auto *storage_rt = storage.second.get_render_target();
			if (storage_rt == nullptr)
			{
				storage_rt = &default_render_target;
			}

			vkb::common::HPPImageMemoryBarrier barrier;
			barrier.old_layout = storage_rt->get_layout(*attachment);
			barrier.new_layout = vk::ImageLayout::eGeneral;

			if (storage_rt->get_layout(*attachment) == barrier.new_layout)
			{
				continue;
			}

			barrier.src_stage_mask  = prev_pass_barrier_info.pipeline_stage;
			barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
			barrier.src_access_mask = prev_pass_barrier_info.image_write_access;
			barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

			assert(*attachment < storage_rt->get_views().size());
			command_buffer.image_memory_barrier(storage_rt->get_views()[*attachment], barrier);
			storage_rt->set_layout(*attachment, barrier.new_layout);
		}
	}
}

void HPPPostProcessingComputePass::draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
{
	transition_images(command_buffer, default_render_target);

	auto &resource_cache = command_buffer.get_device().get_resource_cache();
	auto &shader_module  = resource_cache.request_shader_module(vk::ShaderStageFlagBits::eCompute, cs_source, cs_variant);

	auto &pipeline_layout = resource_cache.request_pipeline_layout({&shader_module});
	command_buffer.bind_pipeline_layout(pipeline_layout);

	const auto &bindings = pipeline_layout.get_descriptor_set_layout(0);

	for (const auto &it : sampled_images)
	{
		if (auto layout_binding = bindings.get_layout_binding(it.first))
		{
			const auto &view = it.second.get_image_view(default_render_target);

			const vk::FormatProperties fmtProps          = get_render_context().get_device().get_gpu().get_format_properties(view.get_format());
			bool                       has_linear_filter = !!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);

			const auto &sampler = it.second.get_sampler() ? *it.second.get_sampler() :
			                                               (has_linear_filter ? *default_sampler : *default_sampler_nearest);

			command_buffer.bind_image(view, sampler, 0, layout_binding->binding, 0);
		}
	}

	for (const auto &it : storage_images)
	{
		if (auto layout_binding = bindings.get_layout_binding(it.first))
		{
			const auto &view = it.second.get_image_view(default_render_target);
			command_buffer.bind_image(view, 0, layout_binding->binding, 0);
		}
	}

	if (!uniform_data.empty())
	{
		auto &render_frame = parent->get_render_context().get_active_frame();
		uniform_alloc = std::make_unique<BufferAllocationCpp>(render_frame.allocate_buffer(vk::BufferUsageFlagBits::eUniformBuffer, uniform_data.size()));
		uniform_alloc->update(uniform_data);
		command_buffer.bind_buffer(uniform_alloc->get_buffer(), uniform_alloc->get_offset(), uniform_alloc->get_size(), 0, 0, 0);
	}

	if (!push_constants_data.empty())
	{
		command_buffer.push_constants(push_constants_data);
	}

	command_buffer.dispatch(n_workgroups.x, n_workgroups.y, n_workgroups.z);
}

HPPPostProcessingPassBase::BarrierInfo HPPPostProcessingComputePass::get_src_barrier_info() const
{
	BarrierInfo info{};
	info.pipeline_stage     = vk::PipelineStageFlagBits::eComputeShader;
	info.image_read_access  = vk::AccessFlagBits::eShaderRead;
	info.image_write_access = vk::AccessFlagBits::eShaderWrite;
	return info;
}

HPPPostProcessingPassBase::BarrierInfo HPPPostProcessingComputePass::get_dst_barrier_info() const
{
	BarrierInfo info{};
	info.pipeline_stage     = vk::PipelineStageFlagBits::eComputeShader;
	info.image_read_access  = vk::AccessFlagBits::eShaderRead;
	info.image_write_access = vk::AccessFlagBits::eShaderWrite;
	return info;
}

}        // namespace vkb
