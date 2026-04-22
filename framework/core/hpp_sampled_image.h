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

#include "core/hpp_image_view.h"
#include "core/hpp_sampler.h"
#include "rendering/render_target.h"
#include <memory>

namespace vkb
{
namespace core
{
/**
 * @brief Cpp version of SampledImage — references an HPPImageView with an optional HPPSampler,
 *        either from a RenderTargetCpp attachment or from a user-created image.
 */
class HPPSampledImage
{
  public:
	HPPSampledImage(const HPPImageView &image_view, HPPSampler *sampler = nullptr);

	HPPSampledImage(uint32_t target_attachment, rendering::RenderTargetCpp *render_target = nullptr, HPPSampler *sampler = nullptr, bool isDepthResolve = false);

	HPPSampledImage(const HPPSampledImage &to_copy);
	HPPSampledImage &operator=(const HPPSampledImage &to_copy);

	HPPSampledImage(HPPSampledImage &&to_move);
	HPPSampledImage &operator=(HPPSampledImage &&to_move);

	~HPPSampledImage() = default;

	void set_image_view(const HPPImageView &new_view);

	void set_image_view(uint32_t new_attachment);

	const uint32_t *get_target_attachment() const;

	const HPPImageView &get_image_view(const rendering::RenderTargetCpp &default_target) const;

	HPPSampler *get_sampler() const;

	void set_sampler(HPPSampler *new_sampler);

	rendering::RenderTargetCpp *get_render_target() const;

	rendering::RenderTargetCpp &get_render_target(rendering::RenderTargetCpp &fallback) const;

	void set_render_target(rendering::RenderTargetCpp *new_render_target);

	bool is_depth_resolve() const;

  private:
	const HPPImageView            *image_view;
	uint32_t                       target_attachment;
	rendering::RenderTargetCpp    *render_target;
	HPPSampler                    *sampler;
	bool                           isDepthResolve;
};

}        // namespace core
}        // namespace vkb
