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

#include "api_vulkan_sample.h"
#include "scene_graph/components/camera.h"

/**
 * @brief Ring Particles Sample
 *
 * 450~500 particles distributed uniformly on a ring. The per-instance
 * position is computed procedurally in the vertex shader from gl_InstanceID
 * (no instance buffer needed):
 *
 *   angle          = (instanceId % ringParticleNum) / ringParticleNum * 2*PI
 *   positionOffset = (cos(angle), sin(angle), 0)
 *   positionOffset *= ringRadius * particleScale
 *
 * Each particle is a small quad (triangle strip). A time uniform spins the
 * ring so the distribution is visible in motion.
 */
class RingParticles : public ApiVulkanSample
{
  public:
	RingParticles();

	virtual ~RingParticles();

	virtual bool prepare(const vkb::ApplicationOptions &options) override;
	virtual void prepare_gui() override;
	virtual void render(float delta_time) override;
	virtual void view_changed() override;
	virtual void on_update_ui_overlay(vkb::Drawer &drawer) override;
	virtual void setup_framebuffer() override;
	virtual void setup_render_pass() override;

  private:
	// UBO layout mirrored in the vertex shader (std140).
	struct UBO
	{
		glm::mat4 projection;
		glm::mat4 view;
		float     ringRadius;              // base ring radius
		float     particleScale;           // scales positionOffset (user formula)
		float     iparticleSizeInstance;    // base particle size (user formula)
		float     time;                    // elapsed time (spin + noise flow)
		uint32_t  ringParticleNum;         // number of particles on the ring
		float     locationFrequencies;     // noise spatial frequency (0.8)
		float     timeFrequencies;         // noise temporal frequency
		float     displace;                // noise displacement base (0.4)
		float     particlePositionNoise;   // noise displacement gain (0.6)
	float     ringWidth1;               // ring width factor for size (1.0)
	float     sizeRate;                // size noise rate (3)
	float     particleReformNoise;     // size noise gain (0.32)
	float     pointSizeScale;          // world-size -> pixel conversion
	};

	UBO ubo{};

	std::unique_ptr<vkb::core::BufferC> uniform_buffer;
	VkPipeline                          pipeline{VK_NULL_HANDLE};
	VkPipelineLayout                    pipeline_layout{VK_NULL_HANDLE};
	VkDescriptorSetLayout              descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet                    descriptor_set{VK_NULL_HANDLE};
	VkDescriptorPool                   descriptor_pool{VK_NULL_HANDLE};

	// User-tunable parameters (GUI).
	uint32_t particle_count{480};                 // 450~500
	float    ring_radius{3.0f};
	float    particle_scale{1.0f};               // user-formula scale on positionOffset
	float    iparticle_size_instance{0.3f};     // base particle size (user formula)
	float    spin_speed{0.3f};                    // ring spin (rad/s)
	bool     rotate_ring{true};

	// Perlin-noise position displacement parameters (user formula).
	float    location_frequencies{0.8f};
	float    time_frequencies{0.5f};
	float    displace{0.4f};
	float    particle_position_noise{0.6f};

	// Perlin-noise size modulation parameters (user formula).
	float    ring_width1{1.0f};
	float    size_rate{1.0f};
	float    particle_reform_noise{0.32f};

	// Point mode: world-size -> pixel conversion for gl_PointSize.
	float    point_size_scale{200.0f};

	// --- Render mode switch: PointParticles (instanced) vs SDF (continuous band) ---
	enum class RenderMode
	{
		PointParticles = 0,
		SDF             = 1,
	};
	RenderMode mode{RenderMode::PointParticles};
	RenderMode gui_mode{RenderMode::PointParticles};

	// SDF-mode parameters + resources (only used when mode == SDF).
	struct SDFUBO
	{
		glm::vec4 viewport;   // center.xy, resolution.xy
		glm::vec4 blur;       // angle, max samples, unused, unused
		glm::vec4 shape;      // base radius, half-width, displacement, location frequency
		glm::vec4 noise;      // time frequency, time, size rate, reform noise
	};
	SDFUBO                            sdf_ubo{};
	std::unique_ptr<vkb::core::BufferC> sdf_uniform_buffer;
	VkPipeline                         sdf_pipeline{VK_NULL_HANDLE};
	VkPipeline                         sdf_compute_pipeline{VK_NULL_HANDLE};
	VkPipelineLayout                   sdf_pipeline_layout{VK_NULL_HANDLE};
	VkDescriptorSetLayout              sdf_descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet                   sdf_descriptor_set{VK_NULL_HANDLE};

	// GPU-generated LUT: vec2(radius, half-width) for each angular sample.
	std::unique_ptr<vkb::core::BufferC> sdf_lut_buffer;
	static constexpr uint32_t          LUT_SIZE{256};

	float    sdf_base_radius{0.45f};
	float    sdf_thickness{0.006f};
	float    sdf_displace{0.04f};
	float    sdf_location_freq{0.8f};      // match point particles' low frequency
	float    sdf_time_freq{0.5f};
	float    sdf_blur_degrees{20.0f};
	uint32_t sdf_blur_samples{32};
	float    sdf_size_rate{1.0f};        // thickness noise rate (matches point sizeRate)
	float    sdf_reform_noise{0.32f};     // thickness noise gain (matches particleReformNoise)

	// --- Rotation-blur post process (layered over the particle render) ---
	bool   enable_rotation_blur{true};
	float  rb_sweep_degrees{20.0f};
	uint32_t rb_samples{32};
	float  rb_center[2]{0.5f, 0.5f};

	// Post-process resources (offscreen particle target + blur pass).
	struct PostUBO
	{
		glm::vec2 center;
		float     total_angle;
		uint32_t  samples;
	};
	PostUBO                            post_ubo{};
	std::unique_ptr<vkb::core::BufferC> post_uniform_buffer;
	VkPipeline                         post_pipeline{VK_NULL_HANDLE};
	VkPipelineLayout                   post_pipeline_layout{VK_NULL_HANDLE};
	VkDescriptorSetLayout              post_descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet                   post_descriptor_set{VK_NULL_HANDLE};

	// Offscreen particle render target (Color + depth) and its own render pass.
	VkRenderPass  offscreen_render_pass{VK_NULL_HANDLE};
	VkFramebuffer offscreen_framebuffer{VK_NULL_HANDLE};
	VkImage       offscreen_color_image{VK_NULL_HANDLE};
	VkDeviceMemory offscreen_color_memory{VK_NULL_HANDLE};
	VkImageView   offscreen_color_view{VK_NULL_HANDLE};
	VkImage       offscreen_depth_image{VK_NULL_HANDLE};
	VkDeviceMemory offscreen_depth_memory{VK_NULL_HANDLE};
	VkImageView   offscreen_depth_view{VK_NULL_HANDLE};
	VkSampler     offscreen_sampler{VK_NULL_HANDLE};
	bool          offscreen_resources_created{false};

	void     create_offscreen_render_pass();
	void     create_offscreen_resources();
	void     destroy_offscreen_resources();
	void     prepare_post_pipelines();
	void     setup_post_descriptor_set_layout();
	void     setup_post_descriptor_set();
	void     update_post_descriptor_writes();
	void     update_post_uniform_buffer();
	PostUBO  build_post_ubo() const;

	// SDF mode
	void     prepare_sdf_pipelines();
	void     setup_sdf_descriptor_set_layout();
	void     setup_sdf_descriptor_set();
	void     update_sdf_uniform_buffer();
	SDFUBO   build_sdf_ubo() const;

	void     prepare_uniform_buffers();
	void     update_uniform_buffers(float delta_time);
	void     prepare_pipelines();
	void     setup_descriptor_pool();
	void     setup_descriptor_set_layout();
	void     setup_descriptor_set();
	void     build_command_buffers() override;};

std::unique_ptr<vkb::Application> create_ring_particles();
