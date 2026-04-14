/* Copyright (c) 2019-2026, Sascha Willems
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

/*
 * GPU-driven particle system using compute shaders for emission, physics, and lifecycle management.
 * Demonstrates Compute + Graphics synchronization with SSBO, atomic counters, and additive blending.
 */

#pragma once

#include "rendering/render_pipeline.h"
#include "rendering/subpasses/forward_subpass.h"
#include "vulkan_sample.h"
#include "camera.h"

#if defined(__ANDROID__)
#	define MAX_PARTICLES 25 * 1024
#else
#	define MAX_PARTICLES 50 * 1024
#endif

class GpuParticleEffects : public vkb::VulkanSampleC
{
  public:
	uint32_t num_particles = MAX_PARTICLES;
	uint32_t work_group_size = 128;

	// Particle structure (must match shader)
	// Total: 48 bytes (3 x vec4 + 1 x float padded to 16)
	struct Particle
	{
		glm::vec4 position;    // xyz = position, w = life (remaining)
		glm::vec4 velocity;    // xyz = velocity, w = max_life
		glm::vec4 color;       // rgba
		glm::vec4 misc;        // x = size, yzw = padding
	};

	// Emitter parameters (must match GLSL std140 layout)
	// std140 rules: vec3+float = 16 bytes, standalone float = 16 bytes (aligned to vec4)
	struct EmitterParams
	{
		alignas(16) glm::vec3 position;
		float                 emit_rate;
		alignas(16) glm::vec3 direction;
		float                 cone_angle;
		alignas(16) glm::vec3 gravity;
		float                 radius;
		float                 particle_size;
		float                 min_life;
		float                 max_life;
		float                 min_speed;
		float                 max_speed;
		int32_t               emitter_type;
		float                 time;
		float                 delta_time;
		uint32_t              particle_count;
		uint32_t              seed;
		float                 wind_strength;
		alignas(16) glm::vec3 wind_direction;
		alignas(16) glm::vec3 extent;
	};

	// Graphics UBO
	struct GraphicsUBO
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec2 screen_dim;
		float     particle_size;
	};

	// Resources for the compute pipeline
	struct Compute
	{
		std::unique_ptr<vkb::core::BufferC> storage_buffer;      // Particle SSBO
		std::unique_ptr<vkb::core::BufferC> dead_indices_buffer; // Stack of dead particle indices
		std::unique_ptr<vkb::core::BufferC> dead_count_buffer;   // Index of the top of the stack (atomic counter)
		std::unique_ptr<vkb::core::BufferC> uniform_buffer;      // Emitter params UBO
		vkb::PipelineLayout*                pipeline_layout_emit{nullptr};
		vkb::PipelineLayout*                pipeline_layout_update{nullptr};
		EmitterParams                       ubo;
	} compute;

	struct Graphics
	{
		std::unique_ptr<vkb::core::BufferC> uniform_buffer;
		vkb::PipelineLayout*                pipeline_layout{nullptr};
		GraphicsUBO                         ubo;
	} graphics;

	GpuParticleEffects();
	~GpuParticleEffects() override;

	virtual void request_gpu_features(vkb::core::PhysicalDeviceC &gpu) override;
	void         prepare_storage_buffers();
	void         prepare_uniform_buffers();
	void         update_compute_uniform_buffers(float delta_time);
	void         update_graphics_uniform_buffers();
	bool         prepare(const vkb::ApplicationOptions &options) override;
	virtual void update(float delta_time) override;
	virtual void draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target) override;

	struct ParticleSubpass : vkb::rendering::SubpassC
	{
		ParticleSubpass(vkb::rendering::RenderContextC &render_context, vkb::ShaderSource &&vertex_shader, vkb::ShaderSource &&fragment_shader);
		virtual void draw(vkb::core::CommandBufferC &command_buffer) override;
		virtual void prepare() override;

		GpuParticleEffects *sample{nullptr};
	};

  private:
	vkb::Camera camera;

	void reset_particles();
	void draw_gui() override;
};

std::unique_ptr<vkb::Application> create_gpu_particle_effects();
