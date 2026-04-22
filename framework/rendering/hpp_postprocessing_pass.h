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
#include "core/command_buffer.h"
#include "core/hpp_shader_module.h"
#include "rendering/render_context.h"
#include "rendering/render_target.h"
#include <functional>

namespace vkb
{
class HPPPostProcessingPipeline;

/**
 * @brief The base of all types of passes in a HPPPostProcessingPipeline (Cpp API).
 */
class HPPPostProcessingPassBase
{
	friend class HPPPostProcessingPipeline;

  public:
	HPPPostProcessingPassBase(HPPPostProcessingPipeline *parent);

	HPPPostProcessingPassBase(const HPPPostProcessingPassBase &)            = delete;
	HPPPostProcessingPassBase &operator=(const HPPPostProcessingPassBase &) = delete;

	HPPPostProcessingPassBase(HPPPostProcessingPassBase &&)            = default;
	HPPPostProcessingPassBase &operator=(HPPPostProcessingPassBase &&) = default;

	virtual ~HPPPostProcessingPassBase() = default;

  protected:
	struct BarrierInfo
	{
		vk::PipelineStageFlags pipeline_stage;
		vk::AccessFlags        image_read_access;
		vk::AccessFlags        image_write_access;
	};

	virtual void prepare(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
	{
		prepared = true;
	}

	virtual void draw(vkb::core::CommandBufferCpp &command_buffer, vkb::rendering::RenderTargetCpp &default_render_target)
	{}

	using HookFunc = std::function<void()>;

	HPPPostProcessingPipeline         *parent{nullptr};
	bool                               prepared{false};
	std::string                        debug_name{};
	vkb::rendering::RenderTargetCpp   *render_target{nullptr};

	HookFunc pre_draw{};
	HookFunc post_draw{};

	vkb::rendering::RenderContextCpp &get_render_context() const;
	vkb::core::HPPShaderSource       &get_triangle_vs() const;

	virtual BarrierInfo get_src_barrier_info() const = 0;
	virtual BarrierInfo get_dst_barrier_info() const = 0;

	BarrierInfo get_predecessor_src_barrier_info(BarrierInfo fallback = {}) const;
};

/**
 * @brief CRTP base of all types of passes in a HPPPostProcessingPipeline (Cpp API).
 */
template <typename Self>
class HPPPostProcessingPass : public HPPPostProcessingPassBase
{
  public:
	using HPPPostProcessingPassBase::HPPPostProcessingPassBase;

	HPPPostProcessingPass(const HPPPostProcessingPass &)            = delete;
	HPPPostProcessingPass &operator=(const HPPPostProcessingPass &) = delete;

	HPPPostProcessingPass(HPPPostProcessingPass &&)            = default;
	HPPPostProcessingPass &operator=(HPPPostProcessingPass &&) = default;

	virtual ~HPPPostProcessingPass() = default;

	Self &set_pre_draw_func(HookFunc &&new_func)
	{
		pre_draw = std::move(new_func);
		return static_cast<Self &>(*this);
	}

	Self &set_post_draw_func(HookFunc &&new_func)
	{
		post_draw = std::move(new_func);
		return static_cast<Self &>(*this);
	}

	vkb::rendering::RenderTargetCpp *get_render_target() const
	{
		return render_target;
	}

	Self &set_render_target(vkb::rendering::RenderTargetCpp *new_render_target)
	{
		render_target = new_render_target;
		return static_cast<Self &>(*this);
	}

	const std::string &get_debug_name() const
	{
		return debug_name;
	}

	Self &set_debug_name(const std::string &new_debug_name)
	{
		debug_name = new_debug_name;
		return static_cast<Self &>(*this);
	}

	HPPPostProcessingPipeline &get_parent() const
	{
		return *parent;
	}
};

}        // namespace vkb
