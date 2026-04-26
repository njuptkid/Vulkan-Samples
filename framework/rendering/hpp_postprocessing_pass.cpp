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

#include "hpp_postprocessing_pass.h"

#include "hpp_postprocessing_pipeline.h"

namespace vkb
{

HPPPostProcessingPassBase::HPPPostProcessingPassBase(HPPPostProcessingPipeline *parent) :
    parent{parent}
{}

vkb::rendering::RenderContextCpp &HPPPostProcessingPassBase::get_render_context() const
{
	return parent->get_render_context();
}

vkb::core::HPPShaderSource &HPPPostProcessingPassBase::get_triangle_vs() const
{
	return parent->triangle_vs;        // friend access
}

HPPPostProcessingPassBase::BarrierInfo HPPPostProcessingPassBase::get_predecessor_src_barrier_info(BarrierInfo fallback) const
{
	const size_t cur_pass_i = parent->get_current_pass_index();
	if (cur_pass_i > 0)
	{
		const auto &prev_pass = *parent->get_passes()[cur_pass_i - 1];
		return prev_pass.get_src_barrier_info();
	}
	else
	{
		return fallback;
	}
}

}        // namespace vkb
