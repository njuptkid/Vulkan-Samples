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

#include "buffer_pool.h"
#include "core/hpp_sampled_image.h"
#include "rendering/hpp_postprocessing_pass.h"
#include "rendering/render_pipeline.h"
#include "rendering/subpass.h"

#include <unordered_set>

namespace vkb
{
/**
 * @brief Hash utility for pairs (used in sampled attachment tracking).
 */
struct HPPPairHasher
{
	template <typename TPair>
	size_t operator()(const TPair &pair) const
	{
		std::hash<decltype(pair.first)>  hash1{};
		std::hash<decltype(pair.second)> hash2{};
		return hash1(pair.first) * 43 + hash2(pair.second);
	}
};

using HPPAttachmentMap   = std::unordered_map<std::string, uint32_t>;
using HPPSampledMap      = std::unordered_map<std::string, core::HPPSampledImage>;
using HPPStorageImageMap = std::unordered_map<std::string, const core::HPPImageView *>;
using HPPAttachmentList  = std::vector<uint32_t>;
using HPPAttachmentSet   = std::unordered_set<uint32_t>;

class HPPPostProcessingRenderPass;

/**
 * @brief A single step of a HPPPostProcessingRenderPass (Cpp API).
 */
class HPPPostProcessingSubpass : public vkb::rendering::SubpassCpp
{
  public:
	HPPPostProcessingSubpass(HPPPostProcessingRenderPass       *parent,
	                         vkb::rendering::RenderContextCpp   &render_context,
	                         vkb::core::HPPShaderSource        &&triangle_vs,
	                         vkb::core::HPPShaderSource        &&fs,
	                         vkb::core::HPPShaderVariant       &&fs_variant = {});

	HPPPostProcessingSubpass(const HPPPostProcessingSubpass &)            = delete;
	HPPPostProcessingSubpass &operator=(const HPPPostProcessingSubpass &) = delete;

	HPPPostProcessingSubpass(HPPPostProcessingSubpass &&to_move);
	HPPPostProcessingSubpass &operator=(HPPPostProcessingSubpass &&) = delete;

	~HPPPostProcessingSubpass() = default;

	const HPPAttachmentMap &get_input_attachments() const
	{
		return input_attachments;
	}

	const HPPSampledMap &get_sampled_images() const
	{
		return sampled_images;
	}

	const HPPStorageImageMap &get_storage_images() const
	{
		return storage_images;
	}

	vkb::core::HPPShaderVariant &get_fs_variant()
	{
		return fs_variant;
	}

	HPPPostProcessingSubpass &set_fs_variant(vkb::core::HPPShaderVariant &&new_variant)
	{
		fs_variant = std::move(new_variant);
		return *this;
	}

	HPPPostProcessingSubpass &set_debug_name(const std::string &name)
	{
		Subpass::set_debug_name(name);
		return *this;
	}

	HPPPostProcessingSubpass &bind_input_attachment(const std::string &name, uint32_t new_input_attachment);

	HPPPostProcessingSubpass &bind_sampled_image(const std::string &name, core::HPPSampledImage &&new_image);

	HPPPostProcessingSubpass &bind_storage_image(const std::string &name, const core::HPPImageView &new_image);

	void unbind_sampled_image(const std::string &name);

	HPPPostProcessingSubpass &set_push_constants(const std::vector<uint8_t> &data);

	template <typename T>
	HPPPostProcessingSubpass &set_push_constants(const T &data)
	{
		push_constants_data.reserve(sizeof(data));
		auto data_ptr = reinterpret_cast<const uint8_t *>(&data);
		push_constants_data.assign(data_ptr, data_ptr + sizeof(data));
		return *this;
	}

	using DrawFunc = std::function<void(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &render_target)>;

	HPPPostProcessingSubpass &set_draw_func(DrawFunc &&new_func);

	static void default_draw_func(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &render_target);

  private:
	HPPPostProcessingRenderPass *parent;

	vkb::core::HPPShaderVariant fs_variant{};

	HPPAttachmentMap   input_attachments{};
	HPPSampledMap      sampled_images{};
	HPPStorageImageMap storage_images{};

	std::vector<uint8_t> push_constants_data{};

	DrawFunc draw_func{&HPPPostProcessingSubpass::default_draw_func};

	void prepare() override;
	void draw(vkb::core::CommandBufferCpp &command_buffer) override;
};

/**
 * @brief A collection of HPPPostProcessingSubpass objects run as a single render pass (Cpp API).
 */
class HPPPostProcessingRenderPass : public HPPPostProcessingPass<HPPPostProcessingRenderPass>
{
  public:
	friend class HPPPostProcessingSubpass;

	HPPPostProcessingRenderPass(HPPPostProcessingPipeline *parent, std::unique_ptr<core::HPPSampler> &&default_sampler = nullptr);

	HPPPostProcessingRenderPass(const HPPPostProcessingRenderPass &)            = delete;
	HPPPostProcessingRenderPass &operator=(const HPPPostProcessingRenderPass &) = delete;

	HPPPostProcessingRenderPass(HPPPostProcessingRenderPass &&)            = default;
	HPPPostProcessingRenderPass &operator=(HPPPostProcessingRenderPass &&) = default;

	void draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target) override;

	HPPPostProcessingSubpass &get_subpass(size_t index)
	{
		assert(index < pipeline.get_subpasses().size());
		return *dynamic_cast<HPPPostProcessingSubpass *>(pipeline.get_subpasses()[index].get());
	}

	// With custom vertex shader and fragment shader
	HPPPostProcessingSubpass &add_subpass(vkb::core::HPPShaderSource &&vs, vkb::core::HPPShaderSource &&fs)
	{
		auto new_subpass = std::make_unique<HPPPostProcessingSubpass>(
		    this, get_render_context(), std::move(vs), std::move(fs));
		auto &new_ref = *new_subpass;
		pipeline.add_subpass(std::move(new_subpass));
		return new_ref;
	}

	// With default postprocessing triangle vertex shader
	HPPPostProcessingSubpass &add_subpass(vkb::core::HPPShaderSource &&fs)
	{
		auto vs_copy      = get_triangle_vs();
		auto new_subpass  = std::make_unique<HPPPostProcessingSubpass>(
		    this, get_render_context(), std::move(vs_copy), std::move(fs));
		auto &new_ref = *new_subpass;
		pipeline.add_subpass(std::move(new_subpass));
		return new_ref;
	}

	template <typename T>
	HPPPostProcessingRenderPass &set_uniform_data(const T &data)
	{
		uniform_data.reserve(sizeof(data));
		auto data_ptr = reinterpret_cast<const uint8_t *>(&data);
		uniform_data.assign(data_ptr, data_ptr + sizeof(data));
		return *this;
	}

	HPPPostProcessingRenderPass &set_uniform_data(const std::vector<uint8_t> &data)
	{
		uniform_data = data;
		return *this;
	}

  private:
	using SampledAttachmentSet = std::unordered_set<std::pair<vkb::rendering::RenderTargetCpp *, uint32_t>, HPPPairHasher>;

	void transition_attachments(const HPPAttachmentSet          &input_attachments,
	                            const SampledAttachmentSet      &sampled_attachments,
	                            const HPPAttachmentSet          &output_attachments,
	                            vkb::core::CommandBufferCpp     &command_buffer,
	                            vkb::rendering::RenderTargetCpp &fallback_render_target);

	void update_load_stores(const HPPAttachmentSet                &input_attachments,
	                        const SampledAttachmentSet             &sampled_attachments,
	                        const HPPAttachmentSet                 &output_attachments,
	                        const vkb::rendering::RenderTargetCpp  &fallback_render_target);

	void prepare_draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &fallback_render_target);

	BarrierInfo get_src_barrier_info() const override;
	BarrierInfo get_dst_barrier_info() const override;

	vkb::rendering::RenderPipelineCpp          pipeline{};
	std::unique_ptr<core::HPPSampler>          default_sampler{};
	std::unique_ptr<core::HPPSampler>          default_sampler_nearest{};
	vkb::rendering::RenderTargetCpp           *draw_render_target{nullptr};
	std::vector<vkb::common::HPPLoadStoreInfo> load_stores{};
	bool                                       load_stores_dirty{true};
	std::vector<uint8_t>                       uniform_data{};
	std::shared_ptr<BufferAllocationCpp>       uniform_buffer_alloc{};
};

}        // namespace vkb
