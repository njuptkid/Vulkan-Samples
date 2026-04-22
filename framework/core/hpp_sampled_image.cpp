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

#include "hpp_sampled_image.h"
#include "core/hpp_image_view.h"
#include "rendering/render_target.h"

namespace vkb
{
namespace core
{
HPPSampledImage::HPPSampledImage(const HPPImageView &image_view, HPPSampler *sampler) :
    image_view{&image_view},
    target_attachment{0},
    render_target{nullptr},
    sampler{sampler},
    isDepthResolve{false}
{}

HPPSampledImage::HPPSampledImage(uint32_t target_attachment, rendering::RenderTargetCpp *render_target, HPPSampler *sampler, bool isDepthResolve) :
    image_view{nullptr},
    target_attachment{target_attachment},
    render_target{render_target},
    sampler{sampler},
    isDepthResolve{isDepthResolve}
{}

HPPSampledImage::HPPSampledImage(const HPPSampledImage &to_copy) :
    image_view{to_copy.image_view},
    target_attachment{to_copy.target_attachment},
    render_target{to_copy.render_target},
    sampler{to_copy.sampler},
    isDepthResolve{to_copy.isDepthResolve}
{}

HPPSampledImage &HPPSampledImage::operator=(const HPPSampledImage &to_copy)
{
	image_view        = to_copy.image_view;
	target_attachment = to_copy.target_attachment;
	render_target     = to_copy.render_target;
	sampler           = to_copy.sampler;
	isDepthResolve    = to_copy.isDepthResolve;
	return *this;
}

HPPSampledImage::HPPSampledImage(HPPSampledImage &&to_move) :
    image_view{std::move(to_move.image_view)},
    target_attachment{std::move(to_move.target_attachment)},
    render_target{std::move(to_move.render_target)},
    sampler{std::move(to_move.sampler)},
    isDepthResolve{std::move(to_move.isDepthResolve)}
{}

HPPSampledImage &HPPSampledImage::operator=(HPPSampledImage &&to_move)
{
	image_view        = std::move(to_move.image_view);
	target_attachment = std::move(to_move.target_attachment);
	render_target     = std::move(to_move.render_target);
	sampler           = std::move(to_move.sampler);
	isDepthResolve    = std::move(to_move.isDepthResolve);
	return *this;
}

const HPPImageView &HPPSampledImage::get_image_view(const rendering::RenderTargetCpp &default_target) const
{
	if (image_view != nullptr)
	{
		return *image_view;
	}
	else
	{
		const auto &target = render_target ? *render_target : default_target;
		assert(target_attachment < target.get_views().size());
		return target.get_views()[target_attachment];
	}
}

const uint32_t *HPPSampledImage::get_target_attachment() const
{
	if (image_view != nullptr)
	{
		return nullptr;
	}
	else
	{
		return &target_attachment;
	}
}

HPPSampler *HPPSampledImage::get_sampler() const
{
	return sampler;
}

void HPPSampledImage::set_sampler(HPPSampler *new_sampler)
{
	sampler = new_sampler;
}

rendering::RenderTargetCpp *HPPSampledImage::get_render_target() const
{
	return render_target;
}

rendering::RenderTargetCpp &HPPSampledImage::get_render_target(rendering::RenderTargetCpp &fallback) const
{
	return render_target ? *render_target : fallback;
}

void HPPSampledImage::set_render_target(rendering::RenderTargetCpp *new_render_target)
{
	render_target = new_render_target;
}

void HPPSampledImage::set_image_view(const HPPImageView &new_view)
{
	image_view = &new_view;
}

void HPPSampledImage::set_image_view(uint32_t new_attachment)
{
	image_view        = nullptr;
	target_attachment = new_attachment;
}

bool HPPSampledImage::is_depth_resolve() const
{
	return isDepthResolve;
}

}        // namespace core
}        // namespace vkb
