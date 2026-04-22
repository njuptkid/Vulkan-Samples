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

#include "rendering/hpp_postprocessing_pass.h"

namespace vkb
{
class HPPPostProcessingRenderPass;

/**
 * @brief A rendering pipeline specialized for fullscreen post-processing (Cpp API).
 */
class HPPPostProcessingPipeline
{
  public:
	friend class HPPPostProcessingPassBase;

	HPPPostProcessingPipeline(vkb::rendering::RenderContextCpp &render_context, vkb::core::HPPShaderSource triangle_vs);

	HPPPostProcessingPipeline(const HPPPostProcessingPipeline &)            = delete;
	HPPPostProcessingPipeline &operator=(const HPPPostProcessingPipeline &) = delete;

	HPPPostProcessingPipeline(HPPPostProcessingPipeline &&)            = delete;
	HPPPostProcessingPipeline &operator=(HPPPostProcessingPipeline &&) = delete;

	virtual ~HPPPostProcessingPipeline() = default;

	void draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target);

	std::vector<std::unique_ptr<HPPPostProcessingPassBase>> &get_passes()
	{
		return passes;
	}

	template <typename TPass = vkb::HPPPostProcessingRenderPass, typename... ConstructorArgs>
	TPass &add_pass(ConstructorArgs &&...args)
	{
		passes.emplace_back(std::make_unique<TPass>(this, std::forward<ConstructorArgs>(args)...));
		auto &added_pass = *dynamic_cast<TPass *>(passes.back().get());
		return added_pass;
	}

	vkb::rendering::RenderContextCpp &get_render_context() const
	{
		return *render_context;
	}

	size_t get_current_pass_index() const
	{
		return current_pass_index;
	}

  private:
	vkb::rendering::RenderContextCpp                       *render_context{nullptr};
	vkb::core::HPPShaderSource                              triangle_vs;
	std::vector<std::unique_ptr<HPPPostProcessingPassBase>> passes{};
	size_t                                                  current_pass_index{0};
};

}        // namespace vkb
