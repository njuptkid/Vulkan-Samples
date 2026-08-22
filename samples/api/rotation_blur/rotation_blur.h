/* Copyright (c) 2025, Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
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

#include <array>
#include <chrono>
#include <vector>

#include "rendering/postprocessing_pipeline.h"
#include "vulkan_sample.h"

/**
 * @brief Rotation Blur Comparison Sample
 *
 * Compares two strategies for simulating rotational/radial motion blur on an image:
 *
 *  - Method A (CircularCompute): Per-pixel circular sampling in a compute shader.
 *    Each output pixel reads N samples along an arc centered on the pixel's polar
 *    angle relative to a user-defined rotation center. Source sampling uses a
 *    plain linear sampler.
 *
 *  - Method B (AnisotropicFrag): Fragment shader with a mipmap+anisotropic sampler.
 *    The same arc sampling pattern is implemented in a fragment shader, but the
 *    sampled texture is configured with a full mipmap chain and anisotropic
 *    filtering enabled. The hardware anisotropic filter widens each sample
 *    footprint along the rotation tangent, allowing fewer taps for equivalent
 *    coverage.
 *
 * The sample measures per-frame GPU time (via frame_times stats) and displays a
 * rolling benchmark so the two methods can be compared side-by-side.
 */
class RotationBlur : public vkb::VulkanSampleC
{
  public:
	RotationBlur();

	virtual ~RotationBlur();

	virtual bool prepare(const vkb::ApplicationOptions &options) override;

	virtual void draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target) override;

	virtual void draw_gui() override;

	virtual void update(float delta_time) override;

  protected:
	virtual void prepare_render_context() override;

  private:
	enum class Method
	{
		CircularCompute = 0,
		AnisotropicFrag = 1,
	};

	// Render target attachment indices
	enum AttachmentIndex
	{
		Swapchain = 0,
		Depth     = 1,
		TempA     = 2,    // compute output (method A) and ping-pong buffer
		AttachmentCount
	};

	// Uniform buffer layout shared by both shaders (std140 compatible, 32 bytes).
	struct alignas(16) Params
	{
		glm::vec2 center;          // rotation center in UV [0,1]
		float     total_angle;      // total arc sweep in radians
		uint32_t  sample_count;     // number of taps along the arc
		float     radius_scale;     // radial scaling factor (1.0 = identity)
		float     _pad[3];          // pad to 32 bytes (std140 struct size = 16x)
	};

	// Per-method rolling benchmark.
	struct Benchmark
	{
		Benchmark &operator+=(const Benchmark &other)
		{
			frame_time_ms += other.frame_time_ms;
			++frames;
			return *this;
		}
		void reset()
		{
			frame_time_ms = 0.0f;
			frames        = 0;
		}
		float    frame_time_ms = 0.0f;
		uint32_t frames        = 0;
	};

	// Active method + GUI-side mirror used to detect switches.
	Method method{Method::CircularCompute};
	Method gui_method{Method::CircularCompute};

	// User-tunable parameters.
	Params params{};
	float  sweep_degrees{20.0f};   // GUI-friendly degrees; converted to radians in build_params

	// Samplers: linear (method A) and anisotropic (method B).
	std::unique_ptr<vkb::core::Sampler> linear_sampler;
	std::unique_ptr<vkb::core::Sampler> aniso_sampler;

	// Source texture (external image). Method A/B sample this directly so that
	// Method B can benefit from the source's mipmap chain + anisotropy. The
	// chain is generated at load time via sg::Image::generate_mipmaps().
	std::unique_ptr<vkb::sg::Image>     source_texture;
	std::unique_ptr<vkb::core::Sampler> source_sampler;

	// Post-processing pipeline: contains the active method's passes. Both
	// methods sample source_texture directly (no intermediate Color draw).
	std::unique_ptr<vkb::PostProcessingPipeline> postprocessing_pipeline;

	// Per-method benchmark accumulators (sampled over a sliding window).
	std::array<Benchmark, 2> benchmarks{};

	// Sliding-window measurement state.
	float    accum_dt_ms[2]   = {0.0f, 0.0f};
	uint32_t accum_frames[2]  = {0, 0};
	float    window_seconds   = 0.5f;
	float    window_accum_time = 0.0f;

	// UI-controlled sample counts for each method (independent tuning).
	// compute_samples is a CAP on the adaptive N in the compute shader
	// (guarantees adjacent taps <= 1 source texel apart, up to this cap).
	uint32_t compute_samples{256};
	uint32_t aniso_samples{8};

	std::unique_ptr<vkb::rendering::RenderTargetC> create_render_target(vkb::core::Image &&swapchain_image);

	void load_source_image();
	void setup_postprocessing_pipeline();
	void rebuild_pipeline_if_needed();

	// Build a fresh Params struct from current GUI state for the active method.
	Params build_params() const;
};

std::unique_ptr<vkb::VulkanSampleC> create_rotation_blur();
