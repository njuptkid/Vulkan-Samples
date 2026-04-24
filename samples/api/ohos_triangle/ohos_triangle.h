/* Copyright (c) 2024, Huawei Technologies Co., Ltd.
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

#include "vulkan_sample.h"

#include "core/buffer.h"
#include "core/hpp_pipeline_layout.h"
#include "core/hpp_render_pass.h"
#include "core/hpp_shader_module.h"
#include "rendering/hpp_compute_pipeline.h"
#include "rendering/render_target.h"
#include <vk_mem_alloc.h>

class OHOSTriangle : public vkb::VulkanSampleCpp
{
	static constexpr uint32_t GRID            = 64;
	static constexpr uint32_t EDGE            = 64;
	static constexpr uint32_t PARTICLE_COUNT  = EDGE * EDGE * 3;
	static constexpr uint32_t PRESSURE_ITERS  = 20;
	static constexpr uint32_t GRID_CELLS      = GRID * GRID * GRID;
	static constexpr uint32_t FLUID_DISPATCH_X = GRID / 8;   // 8
	static constexpr uint32_t FLUID_DISPATCH_Y = GRID / 8;   // 8
	static constexpr uint32_t FLUID_DISPATCH_Z = GRID / 4;   // 16

	struct InitPushConstants
	{
		uint32_t seed;
		float    spawn_radius;
		float    max_life;
		uint32_t edge;
	};

	struct UpdatePushConstants
	{
		float delta_time;
		float time;
		float speed;
		float max_life;
		float grid_w, grid_h, grid_d, rdx;
	};

	struct FluidGridPushConstants
	{
		float grid_w, grid_h, grid_d, dx;
		float rdx, param0, param1, param2;
	};

	struct FluidInjectPushConstants
	{
		float grid_w, grid_h, grid_d, dx;
		float rdx, dt, time, diffusion;
		float force_x, force_y, force_z, force_strength;
		float force_radius, center_x, center_y, center_z;
	};

	struct RenderUBO
	{
		float view_proj[16];        // 64 bytes
		float particle_size;        // 4 bytes
		float _pad[3];              // 12 bytes padding (std140 alignment)
	};

  public:
	OHOSTriangle()          = default;
	~OHOSTriangle() override = default;

	bool prepare(const vkb::ApplicationOptions &options) override;

	void request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const override;

	void draw(vkb::core::CommandBufferCpp &command_buffer,
	          vkb::rendering::RenderTargetCpp &render_target) override;

	void update(float delta_time) override;

  protected:
	void prepare_render_context() override;

  private:
	void create_particle_pipeline();

	// Particle SSBOs (ping-pong positions)
	std::unique_ptr<vkb::core::BufferCpp> particle_pos[2];
	std::unique_ptr<vkb::core::BufferCpp> particle_life;
	std::unique_ptr<vkb::core::BufferCpp> particle_color;

	// Fluid solver SSBOs
	std::unique_ptr<vkb::core::BufferCpp> fluid_vel[2];     // ping-pong velocity (stride-3)
	std::unique_ptr<vkb::core::BufferCpp> fluid_pres[2];    // ping-pong pressure (scalar)
	std::unique_ptr<vkb::core::BufferCpp> fluid_div;        // divergence (scalar)
	std::unique_ptr<vkb::core::BufferCpp> fluid_vort;       // vorticity (stride-3)

	// Quad vertex buffer for instanced rendering
	std::unique_ptr<vkb::core::BufferCpp> quad_vb;

	// Render UBO (MVP + particle size)
	std::unique_ptr<vkb::core::BufferCpp> render_ubo;

	// Compute passes
	std::unique_ptr<vkb::HPPComputePass> init_pass;
	std::unique_ptr<vkb::HPPComputePass> update_pass;

	// Fluid compute passes
	std::unique_ptr<vkb::HPPComputePass> fluid_inject_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_advect_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_boundary_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_divergence_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_boundary_scalar_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_pressure_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_gradient_subtract_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_clear_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_vorticity_pass;
	std::unique_ptr<vkb::HPPComputePass> fluid_vorticity_conf_pass;

	// Graphics pipeline resources
	vkb::core::HPPRenderPass     *particle_render_pass    = nullptr;
	vkb::core::HPPPipelineLayout *particle_pipeline_layout = nullptr;

	uint32_t current_buf = 0;
	uint32_t vel_idx     = 0;
	uint32_t pres_idx    = 0;
	float    elapsed     = 0.0f;
	float    last_dt     = 0.016f;
	bool     initialized = false;

	// Touch state (from napi_init.cpp atomics)
	float    touch_x     = 0.5f;
	float    touch_y     = 0.5f;
	bool     touch_active = false;
};

std::unique_ptr<vkb::Application> create_ohos_triangle();
