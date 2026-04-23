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

#include "ohos_triangle.h"

#include <cmath>

#include "common/hpp_vk_common.h"
#include "core/util/logging.hpp"
#include "filesystem/legacy.h"
#include "rendering/hpp_pipeline_state.h"

#if defined(OHOS)
#include <hilog/log.h>
#define OHOS_LOG_TAG "OHOSTri"
#define OHOS_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, OHOS_LOG_TAG, __VA_ARGS__)
#else
#define OHOS_LOGI(...) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Perspective MVP computation (matches WebGPU reference camera)
// ---------------------------------------------------------------------------

static void compute_mvp(float *out_view_proj, float aspect)
{
	// Perspective projection (column-major)
	float fov      = 45.0f * 3.14159265f / 180.0f;
	float f        = 1.0f / tanf(fov / 2.0f);
	float near_val = 0.1f;
	float far_val  = 10.0f;
	float nf       = 1.0f / (near_val - far_val);

	float proj[16] = {
	    f / aspect, 0, 0, 0,
	    0, f, 0, 0,
	    0, 0, (far_val + near_val) * nf, -1,
	    0, 0, 2 * far_val * near_val * nf, 0};

	// Look-at: eye=(0.5, 1.2, 2.0), target=(0.5, 0.5, 0.5), up=(0,1,0)
	float eye[3]    = {0.5f, 1.2f, 2.0f};
	float target[3] = {0.5f, 0.5f, 0.5f};
	float up[3]     = {0.0f, 1.0f, 0.0f};

	float zAxis[3] = {eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};
	float zLen     = sqrtf(zAxis[0] * zAxis[0] + zAxis[1] * zAxis[1] + zAxis[2] * zAxis[2]);
	zAxis[0] /= zLen;
	zAxis[1] /= zLen;
	zAxis[2] /= zLen;

	float xAxis[3] = {
	    up[1] * zAxis[2] - up[2] * zAxis[1],
	    up[2] * zAxis[0] - up[0] * zAxis[2],
	    up[0] * zAxis[1] - up[1] * zAxis[0]};
	float xLen = sqrtf(xAxis[0] * xAxis[0] + xAxis[1] * xAxis[1] + xAxis[2] * xAxis[2]);
	xAxis[0] /= xLen;
	xAxis[1] /= xLen;
	xAxis[2] /= xLen;

	float yAxis[3] = {
	    zAxis[1] * xAxis[2] - zAxis[2] * xAxis[1],
	    zAxis[2] * xAxis[0] - zAxis[0] * xAxis[2],
	    zAxis[0] * xAxis[1] - zAxis[1] * xAxis[0]};

	float view[16] = {
	    xAxis[0], yAxis[0], zAxis[0], 0,
	    xAxis[1], yAxis[1], zAxis[1], 0,
	    xAxis[2], yAxis[2], zAxis[2], 0,
	    -(xAxis[0] * eye[0] + xAxis[1] * eye[1] + xAxis[2] * eye[2]),
	    -(yAxis[0] * eye[0] + yAxis[1] * eye[1] + yAxis[2] * eye[2]),
	    -(zAxis[0] * eye[0] + zAxis[1] * eye[1] + zAxis[2] * eye[2]),
	    1};

	// view_proj = proj * view (column-major multiply)
	for (int col = 0; col < 4; col++)
	{
		for (int row = 0; row < 4; row++)
		{
			out_view_proj[col * 4 + row] =
			    proj[0 * 4 + row] * view[col * 4 + 0] +
			    proj[1 * 4 + row] * view[col * 4 + 1] +
			    proj[2 * 4 + row] * view[col * 4 + 2] +
			    proj[3 * 4 + row] * view[col * 4 + 3];
		}
	}
}

// ---------------------------------------------------------------------------
// Graphics pipeline creation for particle rendering
// ---------------------------------------------------------------------------

void OHOSTriangle::create_particle_pipeline()
{
	auto &cache  = get_device().get_resource_cache();
	auto  format = get_render_context().get_swapchain().get_format();

	// Render pass: single swapchain attachment
	vkb::rendering::AttachmentCpp attachment{};
	attachment.format  = format;
	attachment.samples = vk::SampleCountFlagBits::e1;
	attachment.usage   = vk::ImageUsageFlagBits::eColorAttachment;

	std::vector<vkb::rendering::AttachmentCpp> attachments = {attachment};
	std::vector<vkb::common::HPPLoadStoreInfo>  load_store  = {
	    {vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore},
	};

	vkb::core::HPPSubpassInfo subpass_info{};
	subpass_info.output_attachments               = {0};
	subpass_info.disable_depth_stencil_attachment = true;

	particle_render_pass = &cache.request_render_pass(attachments, load_store, {subpass_info});

	// Shader modules
	vkb::core::HPPShaderSource vert_source("fluid_particles/glsl/particle.vert.spv");
	vkb::core::HPPShaderSource frag_source("fluid_particles/glsl/particle.frag.spv");

	auto *vert_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_source, {});
	auto *frag_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_source, {});

	particle_pipeline_layout = &cache.request_pipeline_layout({vert_shader, frag_shader});
}

// ---------------------------------------------------------------------------
// VulkanSample overrides
// ---------------------------------------------------------------------------

void OHOSTriangle::request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const
{
	vkb::VulkanSampleCpp::request_layers(requested_layers);

	auto it = requested_layers.find("VK_LAYER_KHRONOS_validation");
	if (it != requested_layers.end())
	{
		it->second = vkb::RequestMode::Optional;
	}
}

bool OHOSTriangle::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleCpp::prepare(options))
	{
		return false;
	}

	auto &device = get_device();

	// Quad vertex buffer: 6 corners for 2 triangles
	const glm::vec2 quad_verts[] = {
	    {-1.0f, -1.0f},
	    {1.0f, -1.0f},
	    {1.0f, 1.0f},
	    {-1.0f, -1.0f},
	    {1.0f, 1.0f},
	    {-1.0f, 1.0f},
	};

	quad_vb = std::make_unique<vkb::core::BufferCpp>(
	    device,
	    static_cast<vk::DeviceSize>(sizeof(quad_verts)),
	    vk::BufferUsageFlagBits::eVertexBuffer,
	    VMA_MEMORY_USAGE_AUTO,
	    VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	quad_vb->update(quad_verts, sizeof(quad_verts));

	// Particle SSBOs
	vk::DeviceSize pos_size = static_cast<vk::DeviceSize>(PARTICLE_COUNT * 3 * sizeof(float));
	vk::DeviceSize life_size = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(float));
	vk::DeviceSize color_size = pos_size;

	auto ssbo_usage = vk::BufferUsageFlagBits::eStorageBuffer;

	for (int i = 0; i < 2; i++)
	{
		particle_pos[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, pos_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
	}

	particle_life = std::make_unique<vkb::core::BufferCpp>(
	    device, life_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);

	particle_color = std::make_unique<vkb::core::BufferCpp>(
	    device, color_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);

	// Fluid solver SSBOs
	vk::DeviceSize fluid_scalar_size = static_cast<vk::DeviceSize>(GRID_CELLS * sizeof(float));
	vk::DeviceSize fluid_vec3_size   = static_cast<vk::DeviceSize>(GRID_CELLS * 3 * sizeof(float));

	for (int i = 0; i < 2; i++)
	{
		fluid_vel[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, fluid_vec3_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
		fluid_pres[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, fluid_scalar_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
	}

	fluid_div = std::make_unique<vkb::core::BufferCpp>(
	    device, fluid_scalar_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
	fluid_vort = std::make_unique<vkb::core::BufferCpp>(
	    device, fluid_vec3_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);

	// Render UBO
	render_ubo = std::make_unique<vkb::core::BufferCpp>(
	    device,
	    static_cast<vk::DeviceSize>(sizeof(RenderUBO)),
	    vk::BufferUsageFlagBits::eUniformBuffer,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Compute MVP and upload
	RenderUBO ubo_data{};
	float     aspect = static_cast<float>(get_render_context().get_swapchain().get_extent().width) /
	                   static_cast<float>(get_render_context().get_swapchain().get_extent().height);
	compute_mvp(ubo_data.view_proj, aspect);
	ubo_data.particle_size = 0.02f;
	render_ubo->update(&ubo_data, sizeof(ubo_data));

	// Compute passes
	init_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/particle_init.comp.spv"});
	init_pass->bind_buffer("PosOut", *particle_pos[0])
	    .bind_buffer("LifeOut", *particle_life)
	    .bind_buffer("ColorOut", *particle_color)
	    .set_dispatch_size(8, 8, 3);

	update_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/particle_update.comp.spv"});
	update_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("PosOut", *particle_pos[1])
	    .bind_buffer("Life", *particle_life)
	    .bind_buffer("ColorOut", *particle_color)
	    .bind_buffer("VelIn", *fluid_vel[0])
	    .set_dispatch_size(8, 8, 3);

	// Fluid compute passes
	fluid_inject_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_inject.comp.spv"});
	fluid_inject_pass->bind_buffer("VelIn", *fluid_vel[0])
	    .bind_buffer("VelOut", *fluid_vel[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_advect_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_advect.comp.spv"});
	fluid_advect_pass->bind_buffer("VelIn", *fluid_vel[0])
	    .bind_buffer("VelOut", *fluid_vel[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_boundary_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_boundary.comp.spv"});
	fluid_boundary_pass->bind_buffer("VelIn", *fluid_vel[0])
	    .bind_buffer("VelOut", *fluid_vel[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_divergence_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_divergence.comp.spv"});
	fluid_divergence_pass->bind_buffer("Vel", *fluid_vel[0])
	    .bind_buffer("Div", *fluid_div)
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_boundary_scalar_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_boundary_scalar.comp.spv"});
	fluid_boundary_scalar_pass->bind_buffer("ScalarIn", *fluid_div)
	    .bind_buffer("ScalarOut", *fluid_div)
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_pressure_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_pressure.comp.spv"});
	fluid_pressure_pass->bind_buffer("PresIn", *fluid_pres[0])
	    .bind_buffer("Div", *fluid_div)
	    .bind_buffer("PresOut", *fluid_pres[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_gradient_subtract_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_gradient_subtract.comp.spv"});
	fluid_gradient_subtract_pass->bind_buffer("Pressure", *fluid_pres[0])
	    .bind_buffer("VelIn", *fluid_vel[0])
	    .bind_buffer("VelOut", *fluid_vel[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_clear_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_clear.comp.spv"});
	fluid_clear_pass->bind_buffer("In", *fluid_pres[0])
	    .bind_buffer("Out", *fluid_pres[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_vorticity_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_vorticity.comp.spv"});
	fluid_vorticity_pass->bind_buffer("Vel", *fluid_vel[0])
	    .bind_buffer("Vort", *fluid_vort)
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	fluid_vorticity_conf_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"fluid_particles/glsl/fluid_vorticity_confinement.comp.spv"});
	fluid_vorticity_conf_pass->bind_buffer("VelIn", *fluid_vel[0])
	    .bind_buffer("Vort", *fluid_vort)
	    .bind_buffer("VelOut", *fluid_vel[1])
	    .set_dispatch_size(FLUID_DISPATCH_X, FLUID_DISPATCH_Y, FLUID_DISPATCH_Z);

	// Graphics pipeline
	create_particle_pipeline();

	// GUI (ImGui) — shows FPS overlay
	create_gui(*window);

	OHOS_LOGI("OHOSTriangle::prepare() COMPLETE — particle system ready (%u particles)", PARTICLE_COUNT);
	return true;
}

void OHOSTriangle::prepare_render_context()
{
	get_render_context().prepare(1, [](vkb::core::HPPImage &&swapchain_image) {
		std::vector<vkb::core::HPPImage> images;
		images.push_back(std::move(swapchain_image));
		auto rt = std::make_unique<vkb::rendering::RenderTargetCpp>(std::move(images));
		rt->set_layout(0, vk::ImageLayout::eUndefined);
		return rt;
	});
}

void OHOSTriangle::update(float delta_time)
{
	elapsed += delta_time;
	last_dt = delta_time;

	// Application base: updates fps / frame_time
	vkb::Application::update(delta_time);

	// GUI — simple FPS overlay (bypasses VulkanSample::update_gui
	// which needs private stats pointer)
	if (has_gui())
	{
		auto &gui = get_gui();
		gui.new_frame();

		ImGui::SetNextWindowBgAlpha(0.3f);
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		bool open = true;
		ImGui::Begin("Top", &open,
		             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
		             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
		             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::Text("%s", get_name().c_str());
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 200.0f);
		ImGui::Text("%.1f FPS (%.2f ms)", fps, frame_time);
		ImGui::End();

		draw_gui();
		gui.update(delta_time);
	}

	// Render loop
	auto command_buffer = get_render_context().begin();
	command_buffer->begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	draw(*command_buffer, get_render_context().get_active_frame().get_render_target());

	command_buffer->end();
	get_render_context().submit(command_buffer);
}

void OHOSTriangle::draw(vkb::core::CommandBufferCpp &command_buffer,
                         vkb::rendering::RenderTargetCpp &render_target)
{
	auto &cache  = get_device().get_resource_cache();
	auto &views  = render_target.get_views();
	auto  extent = render_target.get_extent();

	// Helper: compute → compute buffer barrier
	auto compute_barrier = [&](vkb::core::BufferCpp &buf) {
		vkb::common::HPPBufferMemoryBarrier b{};
		b.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		b.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		b.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		b.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		command_buffer.buffer_memory_barrier(buf, 0, VK_WHOLE_SIZE, b);
	};

	// Grid constants
	const float grid_f = static_cast<float>(GRID);
	const float dx     = 1.0f / grid_f;
	const float rdx    = grid_f;

	// === Compute: Initialize particles (first frame only) ===
	if (!initialized)
	{
		InitPushConstants init_pc{};
		init_pc.seed         = 42;
		init_pc.spawn_radius = 0.2f;
		init_pc.max_life     = 8.0f;
		init_pc.edge         = EDGE;
		init_pass->set_push_constants(init_pc);
		init_pass->draw(command_buffer);

		compute_barrier(*particle_pos[0]);
		compute_barrier(*particle_life);
		compute_barrier(*particle_color);

		initialized = true;
	}

	// =====================================================
	// === Fluid Solver (10 passes per frame)             ===
	// =====================================================
	// Note: vel_idx / pres_idx persist across frames —
	// they track which buffer was last written to.

	// --- 1. Inject rotating force at center ---
	{
		FluidInjectPushConstants pc{};
		pc.grid_w        = grid_f;
		pc.grid_h        = grid_f;
		pc.grid_d        = grid_f;
		pc.dx            = dx;
		pc.rdx           = rdx;
		pc.dt            = last_dt;
		pc.time          = elapsed;
		pc.diffusion     = 0.999f;
		pc.force_x       = sinf(elapsed * 2.0f) * 0.5f;
		pc.force_y       = cosf(elapsed * 1.4f) * 0.5f;
		pc.force_z       = sinf(elapsed * 1.8f) * 0.3f;
		pc.force_strength = 1.0f;
		pc.force_radius  = 0.05f;
		pc.contain_fluid = 1.0f;

		fluid_inject_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);
		fluid_inject_pass->bind_buffer("VelOut", *fluid_vel[1 - vel_idx]);
		fluid_inject_pass->set_push_constants(pc);
		fluid_inject_pass->draw(command_buffer);

		compute_barrier(*fluid_vel[1 - vel_idx]);
		vel_idx = 1 - vel_idx;
	}

	// --- 2. Advect (semi-Lagrangian) ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;
		pc.param0 = last_dt;

		fluid_advect_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);
		fluid_advect_pass->bind_buffer("VelOut", *fluid_vel[1 - vel_idx]);
		fluid_advect_pass->set_push_constants(pc);
		fluid_advect_pass->draw(command_buffer);

		compute_barrier(*fluid_vel[1 - vel_idx]);
		vel_idx = 1 - vel_idx;
	}

	// --- 3. Boundary (velocity reflection) ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;
		pc.param2 = 1.0f;        // contain_fluid

		fluid_boundary_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);
		fluid_boundary_pass->bind_buffer("VelOut", *fluid_vel[1 - vel_idx]);
		fluid_boundary_pass->set_push_constants(pc);
		fluid_boundary_pass->draw(command_buffer);

		compute_barrier(*fluid_vel[1 - vel_idx]);
		vel_idx = 1 - vel_idx;
	}

	// --- 4. Divergence ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;

		fluid_divergence_pass->bind_buffer("Vel", *fluid_vel[vel_idx]);
		fluid_divergence_pass->bind_buffer("Div", *fluid_div);
		fluid_divergence_pass->set_push_constants(pc);
		fluid_divergence_pass->draw(command_buffer);

		compute_barrier(*fluid_div);
	}

	// --- 5. Boundary scalar (divergence) ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;

		fluid_boundary_scalar_pass->bind_buffer("ScalarIn", *fluid_div);
		fluid_boundary_scalar_pass->bind_buffer("ScalarOut", *fluid_div);
		fluid_boundary_scalar_pass->set_push_constants(pc);
		fluid_boundary_scalar_pass->draw(command_buffer);

		compute_barrier(*fluid_div);
	}

	// --- 6. Pressure Jacobi iterations (× PRESSURE_ITERS) ---
	for (uint32_t i = 0; i < PRESSURE_ITERS; i++)
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;

		fluid_pressure_pass->bind_buffer("PresIn", *fluid_pres[pres_idx]);
		fluid_pressure_pass->bind_buffer("Div", *fluid_div);
		fluid_pressure_pass->bind_buffer("PresOut", *fluid_pres[1 - pres_idx]);
		fluid_pressure_pass->set_push_constants(pc);
		fluid_pressure_pass->draw(command_buffer);

		compute_barrier(*fluid_pres[1 - pres_idx]);
		pres_idx = 1 - pres_idx;

		// Boundary scalar (pressure)
		fluid_boundary_scalar_pass->bind_buffer("ScalarIn", *fluid_pres[pres_idx]);
		fluid_boundary_scalar_pass->bind_buffer("ScalarOut", *fluid_pres[1 - pres_idx]);
		fluid_boundary_scalar_pass->set_push_constants(pc);
		fluid_boundary_scalar_pass->draw(command_buffer);

		compute_barrier(*fluid_pres[1 - pres_idx]);
		pres_idx = 1 - pres_idx;
	}

	// --- 7. Gradient subtract ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;

		fluid_gradient_subtract_pass->bind_buffer("Pressure", *fluid_pres[pres_idx]);
		fluid_gradient_subtract_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);
		fluid_gradient_subtract_pass->bind_buffer("VelOut", *fluid_vel[1 - vel_idx]);
		fluid_gradient_subtract_pass->set_push_constants(pc);
		fluid_gradient_subtract_pass->draw(command_buffer);

		compute_barrier(*fluid_vel[1 - vel_idx]);
		compute_barrier(*fluid_pres[pres_idx]);
		vel_idx = 1 - vel_idx;
	}

	// --- 8. Clear pressure (decay) ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;
		pc.param0 = 0.8f;        // viscosity (pressure decay factor)

		fluid_clear_pass->bind_buffer("In", *fluid_pres[pres_idx]);
		fluid_clear_pass->bind_buffer("Out", *fluid_pres[1 - pres_idx]);
		fluid_clear_pass->set_push_constants(pc);
		fluid_clear_pass->draw(command_buffer);

		compute_barrier(*fluid_pres[1 - pres_idx]);
		pres_idx = 1 - pres_idx;
	}

	// --- 9. Vorticity (curl) ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;

		fluid_vorticity_pass->bind_buffer("Vel", *fluid_vel[vel_idx]);
		fluid_vorticity_pass->bind_buffer("Vort", *fluid_vort);
		fluid_vorticity_pass->set_push_constants(pc);
		fluid_vorticity_pass->draw(command_buffer);

		compute_barrier(*fluid_vort);
	}

	// --- 10. Vorticity confinement ---
	{
		FluidGridPushConstants pc{};
		pc.grid_w = grid_f;
		pc.grid_h = grid_f;
		pc.grid_d = grid_f;
		pc.dx     = dx;
		pc.rdx    = rdx;
		pc.param0 = last_dt;
		pc.param1 = 30.0f;        // vorticity_strength

		fluid_vorticity_conf_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);
		fluid_vorticity_conf_pass->bind_buffer("Vort", *fluid_vort);
		fluid_vorticity_conf_pass->bind_buffer("VelOut", *fluid_vel[1 - vel_idx]);
		fluid_vorticity_conf_pass->set_push_constants(pc);
		fluid_vorticity_conf_pass->draw(command_buffer);

		compute_barrier(*fluid_vel[1 - vel_idx]);
		vel_idx = 1 - vel_idx;
	}

	// =====================================================
	// === Particle update (reads fluid velocity)         ===
	// =====================================================
	{
		uint32_t src = current_buf;
		uint32_t dst = 1 - current_buf;

		update_pass->bind_buffer("PosIn", *particle_pos[src]);
		update_pass->bind_buffer("PosOut", *particle_pos[dst]);
		update_pass->bind_buffer("Life", *particle_life);
		update_pass->bind_buffer("ColorOut", *particle_color);
		update_pass->bind_buffer("VelIn", *fluid_vel[vel_idx]);

		UpdatePushConstants update_pc{};
		update_pc.delta_time = last_dt;
		update_pc.time       = elapsed;
		update_pc.speed      = 0.3f;
		update_pc.max_life   = 8.0f;
		update_pc.grid_w     = grid_f;
		update_pc.grid_h     = grid_f;
		update_pc.grid_d     = grid_f;
		update_pc.rdx        = rdx;
		update_pass->set_push_constants(update_pc);
		update_pass->draw(command_buffer);

		// Barrier: compute → vertex shader (SSBO reads in vertex shader)
		vkb::common::HPPBufferMemoryBarrier vert_barrier{};
		vert_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		vert_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
		vert_barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		vert_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;

		command_buffer.buffer_memory_barrier(*particle_pos[dst], 0, VK_WHOLE_SIZE, vert_barrier);
		command_buffer.buffer_memory_barrier(*particle_life, 0, VK_WHOLE_SIZE, vert_barrier);
		command_buffer.buffer_memory_barrier(*particle_color, 0, VK_WHOLE_SIZE, vert_barrier);

		current_buf = dst;
	}

	// === Graphics: Render particles ===

	// Transition swapchain to ColorAttachmentOptimal
	{
		vkb::common::HPPImageMemoryBarrier img_barrier{};
		img_barrier.old_layout      = vk::ImageLayout::eUndefined;
		img_barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		img_barrier.src_access_mask = {};
		img_barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		command_buffer.image_memory_barrier(views[0], img_barrier);
		render_target.set_layout(0, img_barrier.new_layout);
	}

	// Bind graphics pipeline layout
	command_buffer.bind_pipeline_layout(*particle_pipeline_layout);

	// Set pipeline state
	vkb::rendering::HPPVertexInputState vertex_input{};
	vertex_input.bindings   = {{0, sizeof(glm::vec2), vk::VertexInputRate::eVertex}};
	vertex_input.attributes = {
	    {0, 0, vk::Format::eR32G32Sfloat, 0},
	};
	command_buffer.set_vertex_input_state(vertex_input);
	command_buffer.set_input_assembly_state({vk::PrimitiveTopology::eTriangleList, VK_FALSE});

	vkb::rendering::HPPRasterizationState raster{};
	raster.cull_mode  = vk::CullModeFlagBits::eNone;
	raster.front_face = vk::FrontFace::eClockwise;
	command_buffer.set_rasterization_state(raster);
	command_buffer.set_multisample_state({vk::SampleCountFlagBits::e1});
	command_buffer.set_depth_stencil_state({false, false, vk::CompareOp::eAlways});

	// Premultiplied alpha blend
	vkb::rendering::HPPColorBlendAttachmentState blend_attachment{};
	blend_attachment.blend_enable             = VK_TRUE;
	blend_attachment.src_color_blend_factor   = vk::BlendFactor::eOne;
	blend_attachment.dst_color_blend_factor   = vk::BlendFactor::eOneMinusSrcAlpha;
	blend_attachment.color_blend_op           = vk::BlendOp::eAdd;
	blend_attachment.src_alpha_blend_factor   = vk::BlendFactor::eOne;
	blend_attachment.dst_alpha_blend_factor   = vk::BlendFactor::eOneMinusSrcAlpha;
	blend_attachment.alpha_blend_op           = vk::BlendOp::eAdd;

	vkb::rendering::HPPColorBlendState blend{};
	blend.attachments = {blend_attachment};
	command_buffer.set_color_blend_state(blend);

	// Begin render pass
	auto &fb = cache.request_framebuffer(render_target, *particle_render_pass);

	std::vector<vk::ClearValue> clear_values = {
	    vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}},
	};
	command_buffer.begin_render_pass(render_target, *particle_render_pass, fb, clear_values);

	set_viewport_and_scissor(command_buffer, extent);

	// Bind SSBOs and UBO (matching shader layout bindings)
	command_buffer.bind_buffer(*particle_pos[current_buf], 0, VK_WHOLE_SIZE, 0, 0, 0);
	command_buffer.bind_buffer(*particle_life, 0, VK_WHOLE_SIZE, 0, 1, 0);
	command_buffer.bind_buffer(*particle_color, 0, VK_WHOLE_SIZE, 0, 2, 0);
	command_buffer.bind_buffer(*render_ubo, 0, render_ubo->get_size(), 0, 3, 0);

	// Bind quad vertex buffer
	command_buffer.bind_vertex_buffers(0, {std::cref(*quad_vb)}, {0});

	// Instanced draw: 6 vertices × PARTICLE_COUNT instances
	command_buffer.draw(6, PARTICLE_COUNT, 0, 0);

	// GUI overlay (ImGui — FPS / stats)
	if (has_gui())
	{
		get_gui().draw(command_buffer);
	}

	command_buffer.end_render_pass();

	// Present barrier
	{
		vkb::common::HPPImageMemoryBarrier img_barrier{};
		img_barrier.old_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		img_barrier.new_layout      = vk::ImageLayout::ePresentSrcKHR;
		img_barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
		command_buffer.image_memory_barrier(views[0], img_barrier);
		render_target.set_layout(0, img_barrier.new_layout);
	}
}

// ---------------------------------------------------------------------------
// Factory function
// ---------------------------------------------------------------------------

std::unique_ptr<vkb::Application> create_ohos_triangle()
{
	return std::make_unique<OHOSTriangle>();
}
