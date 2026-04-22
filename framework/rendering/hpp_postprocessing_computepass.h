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
#include "common/glm_common.h"
#include "core/hpp_sampled_image.h"
#include "core/hpp_shader_module.h"
#include "rendering/hpp_postprocessing_pass.h"

namespace vkb
{
using HPPSampledImageMap = std::unordered_map<std::string, core::HPPSampledImage>;

/**
 * @brief A compute pass in a HPPPostProcessingPipeline (Cpp API).
 */
class HPPPostProcessingComputePass : public HPPPostProcessingPass<HPPPostProcessingComputePass>
{
  public:
	HPPPostProcessingComputePass(HPPPostProcessingPipeline *parent, const vkb::core::HPPShaderSource &cs_source, const vkb::core::HPPShaderVariant &cs_variant = {},
	                             std::shared_ptr<core::HPPSampler> &&default_sampler = {});

	HPPPostProcessingComputePass(const HPPPostProcessingComputePass &)            = delete;
	HPPPostProcessingComputePass &operator=(const HPPPostProcessingComputePass &) = delete;

	HPPPostProcessingComputePass(HPPPostProcessingComputePass &&)            = default;
	HPPPostProcessingComputePass &operator=(HPPPostProcessingComputePass &&) = default;

	void prepare(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target) override;
	void draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target) override;

	HPPPostProcessingComputePass &set_dispatch_size(glm::tvec3<uint32_t> new_size)
	{
		n_workgroups = new_size;
		return *this;
	}

	glm::tvec3<uint32_t> get_dispatch_size() const
	{
		return n_workgroups;
	}

	const HPPSampledImageMap &get_sampled_images() const
	{
		return sampled_images;
	}

	const HPPSampledImageMap &get_storage_images() const
	{
		return storage_images;
	}

	HPPPostProcessingComputePass &bind_sampled_image(const std::string &name, core::HPPSampledImage &&new_image);

	HPPPostProcessingComputePass &bind_storage_image(const std::string &name, core::HPPSampledImage &&new_image);

	template <typename T>
	HPPPostProcessingComputePass &set_uniform_data(const T &data)
	{
		uniform_data.reserve(sizeof(data));
		auto data_ptr = reinterpret_cast<const uint8_t *>(&data);
		uniform_data.assign(data_ptr, data_ptr + sizeof(data));
		return *this;
	}

	HPPPostProcessingComputePass &set_uniform_data(const std::vector<uint8_t> &data)
	{
		uniform_data = data;
		return *this;
	}

	template <typename T>
	HPPPostProcessingComputePass &set_push_constants(const T &data)
	{
		push_constants_data.reserve(sizeof(data));
		auto data_ptr = reinterpret_cast<const uint8_t *>(&data);
		push_constants_data.assign(data_ptr, data_ptr + sizeof(data));
		return *this;
	}

	HPPPostProcessingComputePass &set_push_constants(const std::vector<uint8_t> &data)
	{
		push_constants_data = data;
		return *this;
	}

  private:
	vkb::core::HPPShaderSource         cs_source;
	vkb::core::HPPShaderVariant        cs_variant;
	glm::tvec3<uint32_t>               n_workgroups{1, 1, 1};

	std::shared_ptr<core::HPPSampler>  default_sampler{};
	std::shared_ptr<core::HPPSampler>  default_sampler_nearest{};
	HPPSampledImageMap                 sampled_images{};
	HPPSampledImageMap                 storage_images{};

	std::vector<uint8_t>               uniform_data{};
	std::unique_ptr<BufferAllocationCpp> uniform_alloc{};
	std::vector<uint8_t>               push_constants_data{};

	void transition_images(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target);

	BarrierInfo get_src_barrier_info() const override;
	BarrierInfo get_dst_barrier_info() const override;
};

}        // namespace vkb
