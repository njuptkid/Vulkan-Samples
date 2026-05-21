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

#include "vulkan_sample.h"

#include "core/buffer.h"
#include "core/hpp_pipeline_layout.h"
#include "core/hpp_render_pass.h"
#include "core/hpp_sampler.h"
#include "core/hpp_shader_module.h"
#include "rendering/hpp_compute_pipeline.h"
#include "rendering/hpp_postprocessing_pipeline.h"
#include "rendering/hpp_postprocessing_renderpass.h"
#include "rendering/render_target.h"

#include <cfloat>        // For FLT_MAX (Part07涡环tracer永不死亡)
#include <glm/glm.hpp>

constexpr uint32_t log2_constexpr(uint32_t n) { return n <= 1 ? 0 : 1 + log2_constexpr(n / 2); }

class VortexRing : public vkb::VulkanSampleCpp
{
	static constexpr uint32_t PARTICLE_COUNT  = 16 * 16 * 16;        // 4096 particles (Part07 standard)
	static constexpr uint32_t WORK_GROUP_SIZE = 64;

	static constexpr uint32_t GRID_DIM          = 64;                                    // Part07 standard: 64³ grid
	static constexpr uint32_t LOG2_GRID_DIM     = log2_constexpr(GRID_DIM);
	static constexpr uint32_t GRID_POINTS       = GRID_DIM * GRID_DIM * GRID_DIM;        // 262144
	static constexpr uint32_t MAX_TRACERS       = 100000;                                // Part07 standard: 100K tracers
	static constexpr uint32_t TRACER_MULTIPLIER = 3;                                     // Part07: numTracersPerCellCubeRoot (inteSiVis.cpp:273)

	struct VortexParams
	{
		float     ring_radius    = 1.0f;
		float     thickness      = 1.0f;
		glm::vec3 direction      = glm::vec3(1.0f, 0.0f, 0.0f);
		float     circulation    = 100.0f;
		float     viscosity      = 0.01f;
		float     stretch_factor = 0.5f;        // FFT-VIC Phase1: Part07 aligned (inteSiVis.cpp)
		uint32_t  ring_segments  = 16;
		uint32_t  tube_rings     = 16;
		uint32_t  tube_radial    = 16;
	};

	struct TracerParams
	{
		float    emit_rate  = 0.0f;           // Part07涡环: 0 (inteSiVis.cpp:280)
		float    max_life   = FLT_MAX;        // Part07涡环: INT_MAX (inteSiVis.cpp:278)
		float    min_life   = FLT_MAX;        // 配合max_life
		uint32_t multiplier = 3;              // Part07: numTracersPerCellCubeRoot (inteSiVis.cpp:273)
	};

	struct InitPushConstants
	{
		float     ring_radius;           // offset 0
		float     thickness;             // offset 4
		float     pad1;                  // offset 8
		float     pad2;                  // offset 12
		glm::vec4 direction;             // offset 16 (vec4 for alignment)
		float     circulation;           // offset 32
		uint32_t  particle_count;        // offset 36
		float     pad3;                  // offset 40 (padding for grid_spacing alignment)
		float     pad4;                  // offset 44
		glm::vec4 grid_spacing;          // offset 48 (vec4 for alignment)
		uint32_t  ring_segments;         // offset 64
		uint32_t  tube_rings;            // offset 68
		uint32_t  tube_radial;           // offset 72
		float     pad5;                  // offset 76 (total 80 bytes)
	};

	struct VelocityPushConstants
	{
		float     delta_time;            // offset 0
		float     time;                  // offset 4
		float     ring_radius;           // offset 8
		float     thickness;             // offset 12
		glm::vec3 direction;             // offset 16 (aligned to 16)
		float     circulation;           // offset 28
		uint32_t  particle_count;        // offset 32
		float     viscosity;             // offset 36
		float     pad1;                  // offset 40 (padding to 48 bytes total)
	};

	struct RenderPushConstants
	{
		glm::mat4 projection;
		glm::mat4 view;
		float     particle_size;
		float     vorticity_threshold;
	};

	struct VelocityComputePC
	{
		uint32_t particle_count;
		float    pad1;
		float    pad2;
		float    pad3;
	};

	struct JacobianPC
	{
		float    delta_step;
		uint32_t particle_count;
		float    pad1;
		float    pad2;
	};

	struct UpdatePC
	{
		float    delta_time;
		float    viscosity;
		uint32_t particle_count;
		float    stretch_factor;
	};

	struct GridHeaderPC
	{
		glm::uvec3 grid_dims;
		glm::vec3  grid_min;
		glm::vec3  grid_spacing;
		uint32_t   grid_points;
		float      pad1;
		float      pad2;
		float      pad3;
	};

	struct GridHashPC
	{
		uint32_t particle_count;
		uint32_t cell_count;
		float    pad1;
		float    pad2;
	};

	struct VelocityGridPC
	{
		glm::uvec3 grid_dims;             // offset 0, size 12
		float      pad1;                  // offset 12, padding to 16
		glm::vec3  grid_min;              // offset 16, size 12
		float      pad2;                  // offset 28, padding to 32
		glm::vec3  grid_spacing;          // offset 32, size 12
		float      pad3;                  // offset 44, padding to 48
		uint32_t   particle_count;        // offset 48
		float      pad4;                  // offset 52, padding to 56
	};

	struct P2GPC
	{
		glm::uvec3 grid_dims;             // offset 0, size 12
		float      ring_radius;           // offset 12, padding to 16
		glm::vec3  grid_min;              // offset 16, size 12
		float      thickness;             // offset 28, padding to 32
		glm::vec3  grid_spacing;          // offset 32, size 12
		float      pad3;                  // offset 44, padding to 48
		uint32_t   particle_count;        // offset 48
		float      pad4;                  // offset 52, padding to 56
	};

	struct FFT3DPC
	{
		uint32_t axis;           // 0 = X, 1 = Y, 2 = Z
		uint32_t inverse;        // 0 = forward, 1 = inverse
	};

	struct SpectralPC
	{
		glm::vec4 grid_spacing;        // xyz = grid_spacing, w = delta_time
		glm::vec4 grid_min;            // xyz = grid_min, w = viscosity
		uint32_t  grid_dim;
	};

	struct JacobianGridPC
	{
		glm::uvec3 grid_dims;           // offset 0, size 12
		float      pad1;                // offset 12, padding to 16
		glm::vec3  grid_spacing;        // offset 16, size 12
		float      pad2;                // offset 28, padding to 32
		uint32_t   grid_points;         // offset 32
		float      pad3;                // offset 36, padding to 40
	};

	struct TracerEmitPC
	{
		glm::uvec3 grid_dims;
		uint32_t   multiplier;        // Part07: tracer count per cell = multiplier³ (default 3 → 27/cell)
		glm::vec3  grid_min;
		float      pad2;
		glm::vec3  grid_spacing;
		float      pad3;
		uint32_t   max_tracers;
		uint32_t   alive_count;
		float      emit_rate;
		float      dt;
		uint32_t   particle_count;
		uint32_t   frame;
		float      min_speed_threshold;
		float      min_life;
		float      max_life;
	};

	struct TracerAdvectPC
	{
		glm::uvec3 grid_dims;           // offset 0, size 12
		float      num_to_emit;         // offset 12, size 4
		glm::vec3  grid_min;            // offset 16, size 12
		float      pad2;                // offset 28, padding to 32
		glm::vec3  grid_spacing;        // offset 32, size 12
		float      pad3;                // offset 44, padding to 48
		uint32_t   max_tracers;         // offset 48
		float      dt;                  // offset 52
		float      min_life;            // offset 56
		float      max_life;            // offset 60
	};

	struct TracerRenderPC
	{
		glm::mat4 view_projection;        // Combined to save space
		float     physical_radius;
		float     viewport_height;
		float     fov_y;
		float     max_life;
		glm::vec3 young_color;        // Young particle color
		float     pad1;               // Padding for alignment
		glm::vec3 old_color;          // Old particle color
		float     pad2;               // Padding for alignment
	};

	struct TracerCompactPC
	{
		uint32_t max_tracers;
		float    num_to_emit;
		float    pad2;
		float    pad3;
	};

	struct SpatialSortPC
	{
		glm::uvec3 grid_dims;             // offset 0, size 12
		float      pad1;                  // offset 12, padding to 16
		glm::vec3  grid_min;              // offset 16, size 12
		float      pad2;                  // offset 28, padding to 32
		glm::vec3  grid_spacing;          // offset 32, size 12
		float      pad3;                  // offset 44, padding to 48
		uint32_t   particle_count;        // offset 48
		float      pad4;                  // offset 52, padding to 56
	};

	struct CellCountPC
	{
		uint32_t particle_count;
		uint32_t grid_points;
		float    pad1;
		float    pad2;
	};

	struct PrefixSumPC
	{
		uint32_t grid_points;
		float    pad1;
		float    pad2;
		float    pad3;
	};

	struct ScatterPC
	{
		glm::uvec3 grid_dims;             // offset 0, size 12
		float      pad1;                  // offset 12, padding to 16
		glm::vec3  grid_min;              // offset 16, size 12
		float      pad2;                  // offset 28, padding to 32
		glm::vec3  grid_spacing;          // offset 32, size 12
		float      pad3;                  // offset 44, padding to 48
		uint32_t   particle_count;        // offset 48
		float      pad4;                  // offset 52, padding to 56
	};

	struct VelocityGridHashPC
	{
		glm::uvec3 grid_dims;             // offset 0, size 12
		float      pad1;                  // offset 12, padding to 16
		glm::vec3  grid_min;              // offset 16, size 12
		float      pad2;                  // offset 28, padding to 32
		glm::vec3  grid_spacing;          // offset 32, size 12
		float      pad3;                  // offset 44, padding to 48
		uint32_t   particle_count;        // offset 48
		float      pad4;                  // offset 52, padding to 56
	};

	struct DiffusePSEPC
	{
		float      viscosity;             // offset 0
		float      dt;                    // offset 4
		float      kernel_radius;         // offset 8 (unused, legacy)
		uint32_t   particle_count;        // offset 12
		glm::uvec3 grid_dims;             // offset 16, size 12
		float      pad1;                  // offset 28, padding to 32
		glm::vec3  grid_min;              // offset 32, size 12
		float      pad2;                  // offset 44, padding to 48
		glm::vec3  grid_spacing;          // offset 48, size 12
		float      pad3;                  // offset 60, padding to 64
	};

	struct UpdateGridPC
	{
		float      delta_time;            // offset 0
		float      viscosity;             // offset 4
		uint32_t   particle_count;        // offset 8
		float      stretch_factor;        // offset 12
		glm::uvec3 grid_dims;             // offset 16, size 12
		float      ring_radius;           // offset 28, padding to 32
		glm::vec3  grid_min;              // offset 32, size 12
		float      thickness;             // offset 44, padding to 48
		glm::vec3  grid_spacing;          // offset 48, size 12
		float      pad3;                  // offset 60, padding to 64
	};

	struct CameraState
	{
		glm::vec3 target   = glm::vec3(0.0f, 0.0f, 0.0f);
		float     distance = 18.0f;
		float     yaw      = 1.5708f;        // π/2 - 看涡环正面
		float     pitch    = 0.0f;
		float     fov      = 75.0f;
	};

	struct GridBounds
	{
		glm::vec3 min_corner = glm::vec3(-18.0f, -18.0f, -18.0f);
		glm::vec3 max_corner = glm::vec3(18.0f, 18.0f, 18.0f);
		glm::vec3 spacing    = glm::vec3(36.0f / GRID_DIM);
	};

  public:
	VortexRing();
	~VortexRing() override = default;

	bool prepare(const vkb::ApplicationOptions &options) override;

	void request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const override;
	void request_gpu_features(vkb::core::PhysicalDeviceCpp &gpu) override;

	void update(float delta_time) override;

	void input_event(const vkb::InputEvent &input_event) override;

  protected:
	void prepare_render_context() override;

  private:
	enum AttachmentIndex
	{
		Swapchain = 0,
		Offscreen = 1,
		AttachmentCount
	};

	void create_offscreen_pipeline();
	void create_copy_pipeline();
	void create_grid_buffers();
	void create_grid_pipelines();
	void update_grid_bounds();
	void draw(vkb::core::CommandBufferCpp     &command_buffer,
	          vkb::rendering::RenderTargetCpp &render_target);

	std::unique_ptr<vkb::core::BufferCpp> particle_pos[2];
	std::unique_ptr<vkb::core::BufferCpp> particle_vort[2];
	std::unique_ptr<vkb::core::BufferCpp> particle_radii;
	std::unique_ptr<vkb::core::BufferCpp> particle_vel;
	std::unique_ptr<vkb::core::BufferCpp> particle_jacobian;

	std::unique_ptr<vkb::core::BufferCpp> vel_grid;
	std::unique_ptr<vkb::core::BufferCpp> jac_grid;
	std::unique_ptr<vkb::core::BufferCpp> omega_grid;
	std::unique_ptr<vkb::core::BufferCpp> omega_grid_imag;
	std::unique_ptr<vkb::core::BufferCpp> vel_grid_imag;

	std::unique_ptr<vkb::core::BufferCpp> spatial_hash;
	std::unique_ptr<vkb::core::BufferCpp> cell_offsets;
	std::unique_ptr<vkb::core::BufferCpp> sorted_indices;
	std::unique_ptr<vkb::core::BufferCpp> cell_count;
	std::unique_ptr<vkb::core::BufferCpp> cell_written;

	std::unique_ptr<vkb::core::BufferCpp> tracer_pos[2];
	std::unique_ptr<vkb::core::BufferCpp> tracer_age[2];
	std::unique_ptr<vkb::core::BufferCpp> tracer_alive_count;
	std::unique_ptr<vkb::core::BufferCpp> tracer_write_counter;

	std::unique_ptr<vkb::HPPComputePass> init_pass;
	std::unique_ptr<vkb::HPPComputePass> velocity_compute_pass;
	std::unique_ptr<vkb::HPPComputePass> jacobian_pass;
	std::unique_ptr<vkb::HPPComputePass> vorton_update_pass;

	std::unique_ptr<vkb::HPPComputePass> spatial_hash_pass;
	std::unique_ptr<vkb::HPPComputePass> cell_count_pass;
	std::unique_ptr<vkb::HPPComputePass> prefix_sum_pass;
	std::unique_ptr<vkb::HPPComputePass> scatter_pass;

	std::unique_ptr<vkb::HPPComputePass> grid_hash_pass;
	std::unique_ptr<vkb::HPPComputePass> velocity_grid_pass;
	std::unique_ptr<vkb::HPPComputePass> velocity_grid_hash_pass;
	std::unique_ptr<vkb::HPPComputePass> jacobian_grid_pass;
	std::unique_ptr<vkb::HPPComputePass> vorton_update_grid_pass;
	std::unique_ptr<vkb::HPPComputePass> vorton_p2g_pass;
	std::unique_ptr<vkb::HPPComputePass> fft_3d_pass;
	std::unique_ptr<vkb::HPPComputePass> spectral_solver_pass;

	std::unique_ptr<vkb::HPPComputePass> diffuse_pse_bidirectional_pass;

	std::unique_ptr<vkb::HPPComputePass> tracer_emit_pass;
	std::unique_ptr<vkb::HPPComputePass> tracer_advect_pass;
	std::unique_ptr<vkb::HPPComputePass> tracer_compact_pass;

	vkb::core::HPPRenderPass     *tracer_render_pass     = nullptr;
	vkb::core::HPPPipelineLayout *tracer_pipeline_layout = nullptr;

	vkb::core::HPPRenderPass     *offscreen_render_pass     = nullptr;
	vkb::core::HPPPipelineLayout *offscreen_pipeline_layout = nullptr;

	vkb::core::HPPRenderPass     *copy_render_pass     = nullptr;
	vkb::core::HPPPipelineLayout *copy_pipeline_layout = nullptr;

	std::unique_ptr<vkb::core::HPPSampler> copy_sampler;

	uint32_t current_buf              = 0;
	uint32_t tracer_current_buf       = 0;
	uint32_t tracer_emit_frame        = 0;
	uint32_t tracer_alive_count_value = 0;
	float    elapsed                  = 0.0f;
	float    last_dt                  = 0.016f;
	bool     initialized              = false;
	bool     debug_init_only          = false;        // FFT-VIC Phase1: enable full simulation
	bool     render_tracers           = true;         // 渲染tracer粒子（debug_init_only=false时）
	bool     render_vortons           = false;        // 同时渲染vorton粒子
	float    fps                      = 0.0f;

	VortexParams vortex_params;
	TracerParams tracer_params;        // Part07涡环tracer配置
	CameraState  camera;
	GridBounds   grid_bounds;
	bool         bounds_staging_ready = false;
	uint32_t     copy_counter         = 0;
	glm::vec3    grid_center          = glm::vec3(0.0f);
	glm::vec3    target_grid_center   = glm::vec3(0.0f);

	std::unique_ptr<vkb::core::BufferCpp> debug_staging_buffer;

	glm::vec2 last_mouse_pos = glm::vec2(0.0f);
	bool      mouse_dragging = false;

	void draw_gui() override;
};

std::unique_ptr<vkb::Application> create_vortex_ring();