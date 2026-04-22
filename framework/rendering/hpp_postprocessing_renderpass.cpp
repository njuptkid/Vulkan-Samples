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

#include "hpp_postprocessing_renderpass.h"

#include "hpp_postprocessing_pipeline.h"
#include "common/vkb_ranges.h"
#include "common/hpp_vk_common.h"

namespace vkb
{
constexpr uint32_t HPP_DEPTH_RESOLVE_BITMASK = 0x80000000;
constexpr uint32_t HPP_ATTACHMENT_BITMASK    = 0x7FFFFFFF;

// ---------------------------------------------------------------------------
// HPPPostProcessingSubpass
// ---------------------------------------------------------------------------

HPPPostProcessingSubpass::HPPPostProcessingSubpass(HPPPostProcessingRenderPass     *parent,
                                                   vkb::rendering::RenderContextCpp &render_context,
                                                   vkb::core::HPPShaderSource      &&triangle_vs,
                                                   vkb::core::HPPShaderSource      &&fs,
                                                   vkb::core::HPPShaderVariant     &&fs_variant) :
    Subpass(render_context, std::move(triangle_vs), std::move(fs)),
    parent{parent},
    fs_variant{std::move(fs_variant)}
{
	set_disable_depth_stencil_attachment(true);

	std::vector<uint32_t> input_attachments{};
	for (const auto &it : this->input_attachments)
	{
		input_attachments.push_back(it.second);
	}
	set_input_attachments(input_attachments);
}

HPPPostProcessingSubpass::HPPPostProcessingSubpass(HPPPostProcessingSubpass &&to_move) :
    Subpass{std::move(to_move)},
    parent{std::move(to_move.parent)},
    fs_variant{std::move(to_move.fs_variant)},
    input_attachments{std::move(to_move.input_attachments)},
    sampled_images{std::move(to_move.sampled_images)}
{}

HPPPostProcessingSubpass &HPPPostProcessingSubpass::bind_input_attachment(const std::string &name, uint32_t new_input_attachment)
{
	input_attachments[name] = new_input_attachment;

	std::vector<uint32_t> input_attachments{};
	for (const auto &it : this->input_attachments)
	{
		input_attachments.push_back(it.second);
	}
	set_input_attachments(input_attachments);

	parent->load_stores_dirty = true;
	return *this;
}

void HPPPostProcessingSubpass::unbind_sampled_image(const std::string &name)
{
	sampled_images.erase(name);
}

HPPPostProcessingSubpass &HPPPostProcessingSubpass::bind_sampled_image(const std::string &name, core::HPPSampledImage &&new_image)
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

	parent->load_stores_dirty = true;
	return *this;
}

HPPPostProcessingSubpass &HPPPostProcessingSubpass::bind_storage_image(const std::string &name, const core::HPPImageView &new_image)
{
	auto it = storage_images.find(name);
	if (it != storage_images.end())
	{
		it->second = &new_image;
	}
	else
	{
		storage_images.emplace(name, &new_image);
	}

	return *this;
}

HPPPostProcessingSubpass &HPPPostProcessingSubpass::set_push_constants(const std::vector<uint8_t> &data)
{
	push_constants_data = data;
	return *this;
}

HPPPostProcessingSubpass &HPPPostProcessingSubpass::set_draw_func(DrawFunc &&new_func)
{
	draw_func = std::move(new_func);
	return *this;
}

void HPPPostProcessingSubpass::prepare()
{
	auto &resource_cache = get_render_context().get_device().get_resource_cache();
	resource_cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, get_vertex_shader());
	resource_cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, get_fragment_shader(), fs_variant);
}

void HPPPostProcessingSubpass::draw(vkb::core::CommandBufferCpp &command_buffer)
{
	auto &resource_cache     = command_buffer.get_device().get_resource_cache();
	auto &vert_shader_module = resource_cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, get_vertex_shader());
	auto &frag_shader_module = resource_cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, get_fragment_shader(), fs_variant);

	std::vector<vkb::core::HPPShaderModule *> shader_modules{&vert_shader_module, &frag_shader_module};

	auto &pipeline_layout = resource_cache.request_pipeline_layout(shader_modules);
	command_buffer.bind_pipeline_layout(pipeline_layout);

	vkb::rendering::HPPRasterizationState rasterization_state;
	rasterization_state.cull_mode = vk::CullModeFlagBits::eNone;
	command_buffer.set_rasterization_state(rasterization_state);

	// Disable depth test — postprocessing passes have no depth attachment
	command_buffer.set_depth_stencil_state({false, false, vk::CompareOp::eAlways});

	// Set color blend state — pipeline_state.reset() clears blend attachments,
	// but the render pass subpass has color outputs that require matching blend state.
	vkb::rendering::HPPColorBlendState blend{};
	vkb::rendering::HPPColorBlendAttachmentState blend_attachment{};
	blend.attachments = {blend_attachment};
	command_buffer.set_color_blend_state(blend);

	auto          &render_target       = *parent->draw_render_target;
	const auto    &target_views        = render_target.get_views();

	if (parent->uniform_buffer_alloc != nullptr)
	{
		auto &uniform_alloc = *parent->uniform_buffer_alloc;
		command_buffer.bind_buffer(uniform_alloc.get_buffer(), uniform_alloc.get_offset(), uniform_alloc.get_size(), 0, 0, 0);
	}

	const auto &bindings = pipeline_layout.get_descriptor_set_layout(0);

	// Bind subpass inputs
	for (const auto &it : input_attachments)
	{
		if (auto layout_binding = bindings.get_layout_binding(it.first))
		{
			assert(it.second < target_views.size());
			command_buffer.bind_input(target_views[it.second], 0, layout_binding->binding, 0);
		}
	}

	// Bind samplers
	for (const auto &it : sampled_images)
	{
		if (auto layout_binding = bindings.get_layout_binding(it.first))
		{
			const auto &view = it.second.get_image_view(render_target);

			const vk::FormatProperties fmtProps          = get_render_context().get_device().get_gpu().get_format_properties(view.get_format());
			bool                       has_linear_filter = !!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);

			const auto &sampler = it.second.get_sampler() ? *it.second.get_sampler() :
			                                               (has_linear_filter ? *parent->default_sampler : *parent->default_sampler_nearest);

			command_buffer.bind_image(view, sampler, 0, layout_binding->binding, 0);
		}
	}

	// Bind storage images
	for (const auto &it : storage_images)
	{
		if (auto layout_binding = bindings.get_layout_binding(it.first))
		{
			command_buffer.bind_image(*it.second, 0, layout_binding->binding, 0);
		}
	}

	command_buffer.push_constants(push_constants_data);

	draw_func(command_buffer, render_target);
}

void HPPPostProcessingSubpass::default_draw_func(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &)
{
	command_buffer.draw(3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// HPPPostProcessingRenderPass
// ---------------------------------------------------------------------------

HPPPostProcessingRenderPass::HPPPostProcessingRenderPass(HPPPostProcessingPipeline *parent, std::unique_ptr<core::HPPSampler> &&default_sampler) :
    HPPPostProcessingPass{parent},
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

		this->default_sampler = std::make_unique<vkb::core::HPPSampler>(get_render_context().get_device(), sampler_info);

		sampler_info.minFilter = vk::Filter::eNearest;
		sampler_info.magFilter = vk::Filter::eNearest;

		this->default_sampler_nearest = std::make_unique<vkb::core::HPPSampler>(get_render_context().get_device(), sampler_info);
	}
}

void HPPPostProcessingRenderPass::update_load_stores(
    const HPPAttachmentSet          &input_attachments,
    const SampledAttachmentSet       &sampled_attachments,
    const HPPAttachmentSet           &output_attachments,
    const vkb::rendering::RenderTargetCpp &fallback_render_target)
{
	if (!load_stores_dirty)
	{
		return;
	}

	const auto &render_target = this->render_target ? *this->render_target : fallback_render_target;

	load_stores.clear();

	for (uint32_t j = 0; j < static_cast<uint32_t>(render_target.get_attachments().size()); j++)
	{
		const bool is_input   = input_attachments.find(j) != input_attachments.end();
		const bool is_sampled = vkb::ranges::find_if(sampled_attachments,
		                                             [&render_target, j](auto &pair) {
			                                             auto *sampled_rt = pair.first ? pair.first : &render_target;
			                                             uint32_t attachment = pair.second & HPP_ATTACHMENT_BITMASK;
			                                             return attachment == j && sampled_rt == &render_target;
		                                             }) != sampled_attachments.end();
		const bool is_output  = output_attachments.find(j) != output_attachments.end();

		vk::AttachmentLoadOp load;
		if (is_input || is_sampled)
		{
			load = vk::AttachmentLoadOp::eLoad;
		}
		else if (is_output)
		{
			load = vk::AttachmentLoadOp::eClear;
		}
		else
		{
			load = vk::AttachmentLoadOp::eDontCare;
		}

		vk::AttachmentStoreOp store;
		if (is_output)
		{
			store = vk::AttachmentStoreOp::eStore;
		}
		else
		{
			store = vk::AttachmentStoreOp::eDontCare;
		}

		load_stores.push_back({load, store});
	}

	pipeline.set_load_store(load_stores);
	load_stores_dirty = false;
}

HPPPostProcessingPassBase::BarrierInfo HPPPostProcessingRenderPass::get_src_barrier_info() const
{
	BarrierInfo info{};
	info.pipeline_stage     = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	info.image_read_access  = vk::AccessFlagBits::eColorAttachmentRead;
	info.image_write_access = vk::AccessFlagBits::eColorAttachmentWrite;
	return info;
}

HPPPostProcessingPassBase::BarrierInfo HPPPostProcessingRenderPass::get_dst_barrier_info() const
{
	BarrierInfo info{};
	info.pipeline_stage     = vk::PipelineStageFlagBits::eFragmentShader;
	info.image_read_access  = vk::AccessFlagBits::eShaderRead;
	info.image_write_access = vk::AccessFlagBits::eShaderWrite;
	return info;
}

static void hpp_ensure_src_access(vk::AccessFlags &src_access, vk::PipelineStageFlags &src_stage, vk::ImageLayout layout)
{
	if (!src_access)
	{
		switch (layout)
		{
			case vk::ImageLayout::eDepthStencilAttachmentOptimal:
				src_stage  = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				src_access = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				src_access |= vk::AccessFlagBits::eDepthStencilAttachmentRead;
				break;
			default:
				src_stage  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
				src_access = vk::AccessFlagBits::eColorAttachmentWrite;
				break;
		}
	}
}

void HPPPostProcessingRenderPass::transition_attachments(const HPPAttachmentSet          &input_attachments,
                                                          const SampledAttachmentSet      &sampled_attachments,
                                                          const HPPAttachmentSet          &output_attachments,
                                                          vkb::core::CommandBufferCpp     &command_buffer,
                                                          vkb::rendering::RenderTargetCpp &fallback_render_target)
{
	auto       &render_target = this->render_target ? *this->render_target : fallback_render_target;
	const auto &views         = render_target.get_views();

	BarrierInfo fallback_barrier_src{};
	fallback_barrier_src.pipeline_stage     = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	fallback_barrier_src.image_read_access  = {};
	fallback_barrier_src.image_write_access = {};
	auto prev_pass_barrier_info             = get_predecessor_src_barrier_info(fallback_barrier_src);

	for (uint32_t input : input_attachments)
	{
		const vk::ImageLayout prev_layout = render_target.get_layout(input);
		if (prev_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			continue;
		}

		hpp_ensure_src_access(prev_pass_barrier_info.image_write_access, prev_pass_barrier_info.pipeline_stage, prev_layout);

		vkb::common::HPPImageMemoryBarrier barrier;
		barrier.old_layout      = render_target.get_layout(input);
		barrier.new_layout      = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.src_access_mask = prev_pass_barrier_info.image_write_access;
		barrier.dst_access_mask = vk::AccessFlagBits::eInputAttachmentRead;
		barrier.src_stage_mask  = prev_pass_barrier_info.pipeline_stage;
		barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eFragmentShader;

		assert(input < views.size());
		command_buffer.image_memory_barrier(views[input], barrier);
		render_target.set_layout(input, vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	for (const auto &sampled : sampled_attachments)
	{
		auto *sampled_rt = sampled.first ? sampled.first : &render_target;

		bool     is_depth_resolve = sampled.second & HPP_DEPTH_RESOLVE_BITMASK;
		uint32_t attachment       = sampled.second & HPP_ATTACHMENT_BITMASK;

		const auto prev_layout = sampled_rt->get_layout(attachment);

		if (prev_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			continue;
		}

		if (prev_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
		{
			prev_pass_barrier_info.pipeline_stage |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			prev_pass_barrier_info.image_read_access |= vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

			if (is_depth_resolve)
			{
				prev_pass_barrier_info.pipeline_stage |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
				prev_pass_barrier_info.image_read_access |= vk::AccessFlagBits::eColorAttachmentWrite;
			}
		}
		else
		{
			hpp_ensure_src_access(prev_pass_barrier_info.image_read_access, prev_pass_barrier_info.pipeline_stage, prev_layout);
		}

		vkb::common::HPPImageMemoryBarrier barrier;
		barrier.old_layout      = prev_layout;
		barrier.new_layout      = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.src_access_mask = prev_pass_barrier_info.image_read_access;
		barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
		barrier.src_stage_mask  = prev_pass_barrier_info.pipeline_stage;
		barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eFragmentShader;

		assert(attachment < sampled_rt->get_views().size());
		command_buffer.image_memory_barrier(sampled_rt->get_views()[attachment], barrier);
		sampled_rt->set_layout(attachment, vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	for (uint32_t output : output_attachments)
	{
		assert(output < views.size());
		const vk::Format      attachment_format = views[output].get_format();
		const bool            is_depth_stencil  = vkb::common::is_depth_format(attachment_format);
		const vk::ImageLayout output_layout     = is_depth_stencil ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eColorAttachmentOptimal;
		if (render_target.get_layout(output) == output_layout)
		{
			continue;
		}

		vkb::common::HPPImageMemoryBarrier barrier;
		barrier.old_layout      = vk::ImageLayout::eUndefined;
		barrier.new_layout      = output_layout;
		barrier.src_access_mask = {};
		if (is_depth_stencil)
		{
			barrier.dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			barrier.src_stage_mask  = vk::PipelineStageFlagBits::eTopOfPipe;
			barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
		}
		else
		{
			barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
			barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}

		command_buffer.image_memory_barrier(views[output], barrier);
		render_target.set_layout(output, output_layout);
	}
}

void HPPPostProcessingRenderPass::prepare_draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &fallback_render_target)
{
	HPPAttachmentSet   input_attachments, output_attachments;
	SampledAttachmentSet sampled_attachments;

	for (auto &step_ptr : pipeline.get_subpasses())
	{
		auto &step = *dynamic_cast<HPPPostProcessingSubpass *>(step_ptr.get());

		for (auto &it : step.get_input_attachments())
		{
			input_attachments.insert(it.second);
		}

		for (auto &it : step.get_sampled_images())
		{
			if (const uint32_t *sampled_attachment = it.second.get_target_attachment())
			{
				auto *image_rt                  = it.second.get_render_target();
				auto  packed_sampled_attachment = *sampled_attachment;

				if (it.second.is_depth_resolve())
				{
					packed_sampled_attachment |= HPP_DEPTH_RESOLVE_BITMASK;
				}

				sampled_attachments.insert({image_rt, packed_sampled_attachment});
			}
		}

		for (uint32_t it : step.get_output_attachments())
		{
			output_attachments.insert(it);
		}
	}

	transition_attachments(input_attachments, sampled_attachments, output_attachments,
	                       command_buffer, fallback_render_target);
	update_load_stores(input_attachments, sampled_attachments, output_attachments,
	                   fallback_render_target);
}

void HPPPostProcessingRenderPass::draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
{
	prepare_draw(command_buffer, default_render_target);

	if (!uniform_data.empty())
	{
		auto &render_frame   = parent->get_render_context().get_active_frame();
		uniform_buffer_alloc = std::make_shared<BufferAllocationCpp>(render_frame.allocate_buffer(vk::BufferUsageFlagBits::eUniformBuffer, uniform_data.size()));
		uniform_buffer_alloc->update(uniform_data);
	}

	draw_render_target = render_target ? render_target : &default_render_target;

	{
		auto &extent = draw_render_target->get_extent();

		vk::Viewport viewport;
		viewport.width    = static_cast<float>(extent.width);
		viewport.height   = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		command_buffer.set_viewport(0, {viewport});

		vk::Rect2D scissor;
		scissor.extent = extent;
		command_buffer.set_scissor(0, {scissor});
	}

	pipeline.draw(command_buffer, *draw_render_target);

	if (parent->get_current_pass_index() < (parent->get_passes().size() - 1))
	{
		command_buffer.end_render_pass();
	}
}

}        // namespace vkb
