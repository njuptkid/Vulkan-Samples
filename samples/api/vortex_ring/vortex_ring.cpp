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

#include "vortex_ring.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "common/hpp_vk_common.h"
#include "core/util/logging.hpp"
#include "platform/input_events.h"
#include "rendering/hpp_pipeline_state.h"

namespace
{
constexpr float PI = 3.14159265358979323846f;
}

void VortexRing::request_layers(std::unordered_map<std::string, vkb::RequestMode> &requested_layers) const
{
	vkb::VulkanSampleCpp::request_layers(requested_layers);

	auto it = requested_layers.find("VK_LAYER_KHRONOS_validation");
	if (it != requested_layers.end())
	{
		it->second = vkb::RequestMode::Optional;
	}
}

VortexRing::VortexRing()
{
	add_device_extension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
}

void VortexRing::request_gpu_features(vkb::core::PhysicalDeviceCpp &gpu)
{
	auto &requested_atomic_float_features = gpu.add_extension_features<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();
	requested_atomic_float_features.shaderBufferFloat32AtomicAdd = true;
}

void VortexRing::create_offscreen_pipeline()
{
	auto &cache  = get_device().get_resource_cache();
	auto  format = get_render_context().get_swapchain().get_format();

	vkb::rendering::AttachmentCpp attachment{};
	attachment.format  = format;
	attachment.samples = vk::SampleCountFlagBits::e1;

	std::vector<vkb::rendering::AttachmentCpp> attachments(AttachmentCount, attachment);
	attachments[0].usage = vk::ImageUsageFlagBits::eColorAttachment;
	attachments[1].usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment;

	std::vector<vkb::common::HPPLoadStoreInfo> load_store = {
	    {vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare},
	    {vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore},
	};

	vkb::core::HPPSubpassInfo subpass_info{};
	subpass_info.output_attachments               = {Offscreen};
	subpass_info.disable_depth_stencil_attachment = true;

	offscreen_render_pass = &cache.request_render_pass(attachments, load_store, {subpass_info});

	vkb::core::HPPShaderSource vert_source("vortex_ring/glsl/tracer.vert.spv");
	vkb::core::HPPShaderSource frag_source("vortex_ring/glsl/tracer.frag.spv");
	auto                      *vert_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_source, {});
	auto                      *frag_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_source, {});
	offscreen_pipeline_layout              = &cache.request_pipeline_layout({vert_shader, frag_shader});
}

void VortexRing::create_copy_pipeline()
{
	auto &cache  = get_device().get_resource_cache();
	auto  format = get_render_context().get_swapchain().get_format();

	vkb::rendering::AttachmentCpp attachment{};
	attachment.format  = format;
	attachment.samples = vk::SampleCountFlagBits::e1;

	std::vector<vkb::rendering::AttachmentCpp> attachments(AttachmentCount, attachment);
	attachments[0].usage = vk::ImageUsageFlagBits::eColorAttachment;
	attachments[1].usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment;

	std::vector<vkb::common::HPPLoadStoreInfo> load_store = {
	    {vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore},
	    {vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare},
	};

	vkb::core::HPPSubpassInfo subpass_info{};
	subpass_info.output_attachments               = {Swapchain};
	subpass_info.disable_depth_stencil_attachment = true;

	copy_render_pass = &cache.request_render_pass(attachments, load_store, {subpass_info});

	vk::SamplerCreateInfo sampler_info{};
	sampler_info.magFilter     = vk::Filter::eLinear;
	sampler_info.minFilter     = vk::Filter::eLinear;
	sampler_info.mipmapMode    = vk::SamplerMipmapMode::eLinear;
	sampler_info.addressModeU  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.addressModeV  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.addressModeW  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.maxAnisotropy = 1.0f;
	sampler_info.maxLod        = 1.0f;
	copy_sampler               = std::make_unique<vkb::core::HPPSampler>(get_device(), sampler_info);

	vkb::core::HPPShaderSource vert_source("vortex_ring/glsl/particle_copy.vert.spv");
	vkb::core::HPPShaderSource frag_source("vortex_ring/glsl/particle_copy.frag.spv");
	auto                      *vert_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_source, {});
	auto                      *frag_shader = &cache.request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_source, {});
	copy_pipeline_layout                   = &cache.request_pipeline_layout({vert_shader, frag_shader});
}

bool VortexRing::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleCpp::prepare(options))
	{
		return false;
	}

	set_name("Vortex Ring");

	auto &device = get_device();

	vk::DeviceSize particle_size = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(glm::vec4));
	vk::DeviceSize vort_size     = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(glm::vec4));
	vk::DeviceSize radii_size    = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(float));
	vk::DeviceSize vel_size      = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(glm::vec4));
	vk::DeviceSize jac_size      = static_cast<vk::DeviceSize>(PARTICLE_COUNT * 3 * sizeof(glm::vec3));

	auto ssbo_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc;

	for (int i = 0; i < 2; i++)
	{
		particle_pos[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, particle_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
		particle_vort[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, vort_size, ssbo_usage, VMA_MEMORY_USAGE_GPU_ONLY);
	}

	debug_staging_buffer = std::make_unique<vkb::core::BufferCpp>(
	    device, sizeof(glm::vec4) * 2, vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);

	particle_radii = std::make_unique<vkb::core::BufferCpp>(
	    device, radii_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

	particle_vel = std::make_unique<vkb::core::BufferCpp>(
	    device, vel_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

	particle_jacobian = std::make_unique<vkb::core::BufferCpp>(
	    device, jac_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

	vk::DeviceSize vel_grid_size = static_cast<vk::DeviceSize>(GRID_POINTS * sizeof(glm::vec4));
	vk::DeviceSize jac_grid_size = static_cast<vk::DeviceSize>(GRID_POINTS * 3 * sizeof(glm::vec3));

	vel_grid = std::make_unique<vkb::core::BufferCpp>(
	    device, vel_grid_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	jac_grid = std::make_unique<vkb::core::BufferCpp>(
	    device, jac_grid_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	omega_grid = std::make_unique<vkb::core::BufferCpp>(
	    device, vel_grid_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
	omega_grid_imag = std::make_unique<vkb::core::BufferCpp>(
	    device, vel_grid_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
	vel_grid_imag = std::make_unique<vkb::core::BufferCpp>(
	    device, vel_grid_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);

	vk::DeviceSize hash_size       = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(uint32_t));
	vk::DeviceSize cell_count_size = static_cast<vk::DeviceSize>(GRID_POINTS * sizeof(uint32_t));
	vk::DeviceSize offsets_size    = static_cast<vk::DeviceSize>((GRID_POINTS + 1) * sizeof(uint32_t));
	vk::DeviceSize indices_size    = static_cast<vk::DeviceSize>(PARTICLE_COUNT * sizeof(uint32_t));

	spatial_hash = std::make_unique<vkb::core::BufferCpp>(
	    device, hash_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	cell_count = std::make_unique<vkb::core::BufferCpp>(
	    device, cell_count_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
	cell_offsets = std::make_unique<vkb::core::BufferCpp>(
	    device, offsets_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	sorted_indices = std::make_unique<vkb::core::BufferCpp>(
	    device, indices_size, vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	cell_written = std::make_unique<vkb::core::BufferCpp>(
	    device, cell_count_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);

	vk::DeviceSize tracer_pos_size   = static_cast<vk::DeviceSize>(MAX_TRACERS * sizeof(glm::vec4));
	vk::DeviceSize tracer_age_size   = static_cast<vk::DeviceSize>(MAX_TRACERS * sizeof(float));
	vk::DeviceSize tracer_count_size = static_cast<vk::DeviceSize>(sizeof(uint32_t));

	for (int i = 0; i < 2; i++)
	{
		tracer_pos[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, tracer_pos_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		tracer_age[i] = std::make_unique<vkb::core::BufferCpp>(
		    device, tracer_age_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	}

	tracer_alive_count = std::make_unique<vkb::core::BufferCpp>(
	    device, tracer_count_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
	LOGI("tracer_alive_count VkBuffer handle: 0x{:x}", uint64_t(static_cast<VkBuffer>(tracer_alive_count->get_handle())));

	tracer_write_counter = std::make_unique<vkb::core::BufferCpp>(
	    device, tracer_count_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);
	LOGI("tracer_write_counter VkBuffer handle: 0x{:x}", uint64_t(static_cast<VkBuffer>(tracer_write_counter->get_handle())));

	LOGI("omega_grid VkBuffer handle: 0x{:x}", uint64_t(static_cast<VkBuffer>(omega_grid->get_handle())));
	LOGI("omega_grid_imag VkBuffer handle: 0x{:x}", uint64_t(static_cast<VkBuffer>(omega_grid_imag->get_handle())));
	LOGI("vel_grid_imag VkBuffer handle: 0x{:x}", uint64_t(static_cast<VkBuffer>(vel_grid_imag->get_handle())));

	init_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/vortex_init.comp.spv"});
	init_pass->bind_buffer("PosOut", *particle_pos[0])
	    .bind_buffer("VortOut", *particle_vort[0])
	    .bind_buffer("RadiiOut", *particle_radii)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	velocity_compute_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/velocity_compute.comp.spv"});
	velocity_compute_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("VelOut", *particle_vel)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	jacobian_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/velocity_jacobian.comp.spv"});
	jacobian_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("JacOut", *particle_jacobian)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	vorton_update_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/vorton_update.comp.spv"});
	vorton_update_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("VelIn", *particle_vel)
	    .bind_buffer("JacIn", *particle_jacobian)
	    .bind_buffer("PosOut", *particle_pos[1])
	    .bind_buffer("VortOut", *particle_vort[1])
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	uint32_t grid_dispatch_x = (GRID_DIM + 3) / 4;
	uint32_t grid_dispatch_y = (GRID_DIM + 3) / 4;
	uint32_t grid_dispatch_z = (GRID_DIM + 3) / 4;

	spatial_hash_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/spatial_hash.comp.spv"});
	spatial_hash_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("SpatialHashOut", *spatial_hash)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	cell_count_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/cell_count.comp.spv"});
	cell_count_pass->bind_buffer("SpatialHashIn", *spatial_hash)
	    .bind_buffer("CellCountOut", *cell_count)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	prefix_sum_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/prefix_sum.comp.spv"});
	prefix_sum_pass->bind_buffer("CellCountIn", *cell_count)
	    .bind_buffer("CellOffsetsOut", *cell_offsets)
	    .set_dispatch_size(1, 1, 1);

	scatter_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/scatter.comp.spv"});
	scatter_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("SpatialHashIn", *spatial_hash)
	    .bind_buffer("CellOffsetsIn", *cell_offsets)
	    .bind_buffer("CellWritten", *cell_written)
	    .bind_buffer("SortedIndicesOut", *sorted_indices)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	velocity_grid_hash_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/velocity_grid_hash.comp.spv"});
	velocity_grid_hash_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("CellOffsets", *cell_offsets)
	    .bind_buffer("SortedIndices", *sorted_indices)
	    .bind_buffer("VelGridOut", *vel_grid)
	    .set_dispatch_size(grid_dispatch_x, grid_dispatch_y, grid_dispatch_z);

	velocity_grid_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/velocity_grid_direct.comp.spv"});
	velocity_grid_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("VelGridOut", *vel_grid)
	    .set_dispatch_size(grid_dispatch_x, grid_dispatch_y, grid_dispatch_z);

	jacobian_grid_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/jacobian_grid.comp.spv"});
	jacobian_grid_pass->bind_buffer("VelGridIn", *vel_grid)
	    .bind_buffer("JacGridOut", *jac_grid)
	    .set_dispatch_size(grid_dispatch_x, grid_dispatch_y, grid_dispatch_z);

	vorton_update_grid_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/vorton_update_grid.comp.spv"});
	vorton_update_grid_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("VelGrid", *vel_grid)
	    .bind_buffer("JacGrid", *jac_grid)
	    .bind_buffer("OmegaGrid", *omega_grid)
	    .bind_buffer("PosOut", *particle_pos[1])
	    .bind_buffer("VortOut", *particle_vort[1])
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	vorton_p2g_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/vorton_p2g.comp.spv"});
	vorton_p2g_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("OmegaGridOut", *omega_grid)
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	fft_3d_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/fft_3d.comp.spv"});
	fft_3d_pass->bind_buffer("GridReal", *omega_grid)
	    .bind_buffer("GridImag", *omega_grid_imag)
	    .set_dispatch_size(GRID_DIM, GRID_DIM, 1);

	spectral_solver_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/spectral_solver.comp.spv"});
	spectral_solver_pass->bind_buffer("OmegaReal", *omega_grid)
	    .bind_buffer("OmegaImag", *omega_grid_imag)
	    .bind_buffer("VelReal", *vel_grid)
	    .bind_buffer("VelImag", *vel_grid_imag)
	    .set_dispatch_size(GRID_DIM / 8, GRID_DIM / 8, GRID_DIM / 8);

	tracer_emit_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/tracer_emit.comp.spv"});
	tracer_emit_pass->bind_buffer("VortonPos", *particle_pos[0])
	    .bind_buffer("TracerPosIn", *tracer_pos[0])
	    .bind_buffer("TracerAgeIn", *tracer_age[0])
	    .bind_buffer("TracerPosOut", *tracer_pos[1])
	    .bind_buffer("TracerAgeOut", *tracer_age[1])
	    .bind_buffer("AliveCount", *tracer_alive_count)
	    .set_dispatch_size((MAX_TRACERS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	tracer_advect_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/tracer_advect.comp.spv"});
	tracer_advect_pass->bind_buffer("VelGrid", *vel_grid)
	    .bind_buffer("TracerPosIn", *tracer_pos[0])
	    .bind_buffer("TracerAgeIn", *tracer_age[0])
	    .bind_buffer("TracerPosOut", *tracer_pos[1])
	    .bind_buffer("TracerAgeOut", *tracer_age[1])
	    .bind_buffer("AliveCount", *tracer_alive_count)
	    .set_dispatch_size((MAX_TRACERS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	// Tracer compaction pass: remove dead tracers
	tracer_compact_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/tracer_compact.comp.spv"});
	tracer_compact_pass->bind_buffer("TracerPosIn", *tracer_pos[1])
	    .bind_buffer("TracerAgeIn", *tracer_age[1])
	    .bind_buffer("TracerPosOut", *tracer_pos[0])
	    .bind_buffer("TracerAgeOut", *tracer_age[0])
	    .bind_buffer("AliveCountIn", *tracer_alive_count)
	    .bind_buffer("AliveCountOut", *tracer_alive_count)
	    .bind_buffer("WriteCounter", *tracer_write_counter)
	    .set_dispatch_size((MAX_TRACERS + 255) / 256, 1, 1);

	// PSE bidirectional exchange (single pass with atomic operations - Part07 algorithm)
	diffuse_pse_bidirectional_pass = std::make_unique<vkb::HPPComputePass>(
	    get_render_context(),
	    vkb::core::HPPShaderSource{"vortex_ring/glsl/diffuse_pse_bidirectional.comp.spv"});
	diffuse_pse_bidirectional_pass->bind_buffer("PosIn", *particle_pos[0])
	    .bind_buffer("VortIn", *particle_vort[0])
	    .bind_buffer("Radii", *particle_radii)
	    .bind_buffer("CellOffsets", *cell_offsets)
	    .bind_buffer("SortedIndices", *sorted_indices)
	    .bind_buffer("VortOut", *particle_vort[1])
	    .set_dispatch_size((PARTICLE_COUNT + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE, 1, 1);

	create_offscreen_pipeline();
	create_copy_pipeline();

	create_gui(*window);

	return true;
}

void VortexRing::prepare_render_context()
{
	get_render_context().prepare(1, [](vkb::core::HPPImage &&swapchain_image) {
		auto &device = swapchain_image.get_device();
		auto  extent = swapchain_image.get_extent();
		auto  format = swapchain_image.get_format();

		std::vector<vkb::core::HPPImage> images;
		images.push_back(std::move(swapchain_image));

		vk::ImageUsageFlags rt_usage =
		    vk::ImageUsageFlagBits::eColorAttachment |
		    vk::ImageUsageFlagBits::eSampled |
		    vk::ImageUsageFlagBits::eInputAttachment;

		for (uint32_t i = 1; i < AttachmentCount; ++i)
		{
			images.emplace_back(device, vk::Extent3D{extent.width, extent.height, 1},
			                    format, rt_usage, VMA_MEMORY_USAGE_GPU_ONLY);
		}

		auto rt = std::make_unique<vkb::rendering::RenderTargetCpp>(std::move(images));
		for (uint32_t i = 0; i < AttachmentCount; ++i)
		{
			rt->set_layout(i, vk::ImageLayout::eUndefined);
		}
		return rt;
	});
}

void VortexRing::update(float delta_time)
{
	static uint32_t frame_count = 0;
	frame_count++;
	if (frame_count % 60 == 1 && frame_count > 1)
	{
		get_device().wait_idle();
		auto *mapped = debug_staging_buffer->map();
		if (mapped)
		{
			glm::vec4 *data = reinterpret_cast<glm::vec4 *>(mapped);
			LOGI("[Diagnostic] Frame {}, Vorton[0] Pos: ({:.4f}, {:.4f}, {:.4f}, {:.4f}), Vort: ({:.4f}, {:.4f}, {:.4f}, {:.4f})",
			     frame_count - 1, data[0].x, data[0].y, data[0].z, data[0].w,
			     data[1].x, data[1].y, data[1].z, data[1].w);
			debug_staging_buffer->unmap();
		}
	}

	elapsed += delta_time;
	last_dt = delta_time;

	auto smooth_fps = fps * 0.95f + (1.0f / delta_time) * 0.05f;
	fps             = smooth_fps;

	vkb::Application::update(delta_time);

	if (has_gui())
	{
		auto &gui = get_gui();
		gui.new_frame();
		draw_gui();
		gui.update(delta_time);
	}

	auto command_buffer = get_render_context().begin();
	command_buffer->begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	draw(*command_buffer, get_render_context().get_active_frame().get_render_target());

	command_buffer->end();
	get_render_context().submit(command_buffer);
}

void VortexRing::draw(vkb::core::CommandBufferCpp     &command_buffer,
                      vkb::rendering::RenderTargetCpp &render_target)
{
	auto &cache  = get_device().get_resource_cache();
	auto &views  = render_target.get_views();
	auto  extent = render_target.get_extent();

	for (uint32_t i = 0; i < AttachmentCount; ++i)
	{
		render_target.set_layout(i, vk::ImageLayout::eUndefined);
	}

	auto compute_barrier = [&](vkb::core::BufferCpp &buf) {
		vkb::common::HPPBufferMemoryBarrier b{};
		b.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		b.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		b.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		b.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		command_buffer.buffer_memory_barrier(buf, 0, VK_WHOLE_SIZE, b);
	};

	if (!initialized)
	{
		InitPushConstants init_pc{};
		init_pc.ring_radius    = vortex_params.ring_radius;
		init_pc.thickness      = vortex_params.thickness;
		init_pc.direction      = glm::vec4(vortex_params.direction, 0.0f);
		init_pc.circulation    = vortex_params.circulation;
		init_pc.particle_count = PARTICLE_COUNT;
		init_pc.grid_spacing   = glm::vec4(grid_bounds.spacing, 0.0f);
		init_pc.ring_segments  = vortex_params.ring_segments;
		init_pc.tube_rings     = vortex_params.tube_rings;
		init_pc.tube_radial    = vortex_params.tube_radial;

		init_pass->set_push_constants(init_pc);
		init_pass->draw(command_buffer);

		compute_barrier(*particle_pos[current_buf]);
		compute_barrier(*particle_vort[current_buf]);
		compute_barrier(*particle_radii);

		initialized = true;
	}

	// Debug mode: skip compute passes, only render initialized vortons
	if (!debug_init_only)
	{
		uint32_t src = current_buf;
		uint32_t dst = 1 - current_buf;

		// Clear omega_grid, omega_grid_imag, and vel_grid_imag buffers using vkCmdFillBuffer
		vkCmdFillBuffer(command_buffer.get_handle(), omega_grid->get_handle(), 0, GRID_POINTS * sizeof(glm::vec4), 0);
		vkCmdFillBuffer(command_buffer.get_handle(), omega_grid_imag->get_handle(), 0, GRID_POINTS * sizeof(glm::vec4), 0);
		vkCmdFillBuffer(command_buffer.get_handle(), vel_grid_imag->get_handle(), 0, GRID_POINTS * sizeof(glm::vec4), 0);

		vkb::common::HPPBufferMemoryBarrier clear_barrier{};
		clear_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eTransfer;
		clear_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		clear_barrier.src_access_mask = vk::AccessFlagBits::eTransferWrite;
		clear_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		command_buffer.buffer_memory_barrier(*omega_grid, 0, VK_WHOLE_SIZE, clear_barrier);
		command_buffer.buffer_memory_barrier(*omega_grid_imag, 0, VK_WHOLE_SIZE, clear_barrier);
		command_buffer.buffer_memory_barrier(*vel_grid_imag, 0, VK_WHOLE_SIZE, clear_barrier);

		P2GPC p2g_pc{};
		p2g_pc.grid_dims      = glm::uvec3(GRID_DIM, GRID_DIM, GRID_DIM);
		p2g_pc.ring_radius    = vortex_params.ring_radius;
		p2g_pc.grid_min       = grid_bounds.min_corner;
		p2g_pc.thickness      = vortex_params.thickness;
		p2g_pc.grid_spacing   = grid_bounds.spacing;
		p2g_pc.particle_count = PARTICLE_COUNT;

		vorton_p2g_pass->bind_buffer("PosIn", *particle_pos[src])
		    .bind_buffer("VortIn", *particle_vort[src])
		    .bind_buffer("Radii", *particle_radii)
		    .bind_buffer("OmegaGridOut", *omega_grid)
		    .set_push_constants(p2g_pc);
		vorton_p2g_pass->draw(command_buffer);
		compute_barrier(*omega_grid);

		// Forward 3D FFT on OmegaGrid (OmegaReal and OmegaImag)
		FFT3DPC fft_pc{};
		fft_pc.inverse = 0;        // 0 = forward

		// X-axis
		fft_pc.axis = 0;
		fft_3d_pass->bind_buffer("GridReal", *omega_grid)
		    .bind_buffer("GridImag", *omega_grid_imag)
		    .set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Y-axis
		fft_pc.axis = 1;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Z-axis
		fft_pc.axis = 2;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Spectral Vector Poisson Solver & Viscous Decay
		SpectralPC spectral_pc{};
		spectral_pc.grid_spacing = glm::vec4(grid_bounds.spacing, last_dt);
		spectral_pc.grid_min     = glm::vec4(grid_bounds.min_corner, vortex_params.viscosity);

		spectral_solver_pass->bind_buffer("OmegaReal", *omega_grid)
		    .bind_buffer("OmegaImag", *omega_grid_imag)
		    .bind_buffer("VelReal", *vel_grid)
		    .bind_buffer("VelImag", *vel_grid_imag)
		    .set_push_constants(spectral_pc);
		spectral_solver_pass->draw(command_buffer);
		compute_barrier(*vel_grid);
		compute_barrier(*vel_grid_imag);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Inverse 3D FFT on OmegaGrid (diffused vorticity)
		fft_pc.inverse = 1;        // 1 = inverse
		fft_pc.axis = 0;           // X-axis
		fft_3d_pass->bind_buffer("GridReal", *omega_grid)
		    .bind_buffer("GridImag", *omega_grid_imag)
		    .set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Y-axis
		fft_pc.axis = 1;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Z-axis
		fft_pc.axis = 2;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*omega_grid);
		compute_barrier(*omega_grid_imag);

		// Inverse 3D FFT on VelGrid (VelReal and VelImag)
		fft_pc.inverse = 1;        // 1 = inverse

		// X-axis
		fft_pc.axis = 0;
		fft_3d_pass->bind_buffer("GridReal", *vel_grid)
		    .bind_buffer("GridImag", *vel_grid_imag)
		    .set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*vel_grid);
		compute_barrier(*vel_grid_imag);

		// Y-axis
		fft_pc.axis = 1;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*vel_grid);
		compute_barrier(*vel_grid_imag);

		// Z-axis
		fft_pc.axis = 2;
		fft_3d_pass->set_push_constants(fft_pc);
		fft_3d_pass->draw(command_buffer);
		compute_barrier(*vel_grid);

		// Jacobian Grid Pass
		JacobianGridPC jac_grid_pc{};
		jac_grid_pc.grid_dims    = glm::uvec3(GRID_DIM, GRID_DIM, GRID_DIM);
		jac_grid_pc.grid_spacing = grid_bounds.spacing;
		jac_grid_pc.grid_points  = GRID_POINTS;

		jacobian_grid_pass->bind_buffer("VelGridIn", *vel_grid)
		    .bind_buffer("JacGridOut", *jac_grid)
		    .set_push_constants(jac_grid_pc);
		jacobian_grid_pass->draw(command_buffer);
		compute_barrier(*jac_grid);

		// G2P update and particle advection
		UpdateGridPC update_grid_pc{};
		update_grid_pc.delta_time     = last_dt;
		update_grid_pc.viscosity      = vortex_params.viscosity;
		update_grid_pc.particle_count = PARTICLE_COUNT;
		update_grid_pc.stretch_factor = vortex_params.stretch_factor;
		update_grid_pc.grid_dims      = glm::uvec3(GRID_DIM, GRID_DIM, GRID_DIM);
		update_grid_pc.grid_min       = grid_bounds.min_corner;
		update_grid_pc.grid_spacing   = grid_bounds.spacing;

		vorton_update_grid_pass->bind_buffer("PosIn", *particle_pos[src])
		    .bind_buffer("VortIn", *particle_vort[src])
		    .bind_buffer("VelGrid", *vel_grid)
		    .bind_buffer("JacGrid", *jac_grid)
		    .bind_buffer("OmegaGrid", *omega_grid)
		    .bind_buffer("PosOut", *particle_pos[dst])
		    .bind_buffer("VortOut", *particle_vort[dst])
		    .set_push_constants(update_grid_pc);
		vorton_update_grid_pass->draw(command_buffer);

		compute_barrier(*particle_pos[dst]);
		compute_barrier(*particle_vort[dst]);

		// Tracer pipeline
		uint32_t tracer_src = tracer_current_buf;
		uint32_t tracer_dst = 1 - tracer_current_buf;

		TracerEmitPC emit_pc{};
		emit_pc.grid_dims           = glm::uvec3(GRID_DIM, GRID_DIM, GRID_DIM);
		emit_pc.multiplier          = tracer_params.multiplier;        // Part07: 27 tracers per cell
		emit_pc.grid_min            = grid_bounds.min_corner;
		emit_pc.grid_spacing        = grid_bounds.spacing;
		emit_pc.max_tracers         = MAX_TRACERS;
		emit_pc.alive_count         = tracer_alive_count_value;
		emit_pc.emit_rate           = tracer_params.emit_rate;        // Part07涡环: 0
		emit_pc.dt                  = last_dt;
		emit_pc.particle_count      = PARTICLE_COUNT;
		emit_pc.frame               = tracer_emit_frame;
		emit_pc.min_speed_threshold = 0.5f;
		emit_pc.min_life            = tracer_params.min_life;        // Part07涡环: FLT_MAX
		emit_pc.max_life            = tracer_params.max_life;        // Part07涡环: FLT_MAX

		tracer_emit_pass->bind_buffer("VortonPos", *particle_pos[dst])
		    .bind_buffer("TracerPosIn", *tracer_pos[tracer_src])
		    .bind_buffer("TracerAgeIn", *tracer_age[tracer_src])
		    .bind_buffer("TracerPosOut", *tracer_pos[tracer_dst])
		    .bind_buffer("TracerAgeOut", *tracer_age[tracer_dst])
		    .bind_buffer("AliveCount", *tracer_alive_count)
		    .set_push_constants(emit_pc);
		tracer_emit_pass->draw(command_buffer);

		compute_barrier(*tracer_pos[tracer_dst]);
		compute_barrier(*tracer_age[tracer_dst]);
		compute_barrier(*tracer_alive_count);

		TracerAdvectPC advect_pc{};
		advect_pc.grid_dims    = glm::uvec3(GRID_DIM, GRID_DIM, GRID_DIM);
		advect_pc.grid_min     = grid_bounds.min_corner;
		advect_pc.grid_spacing = grid_bounds.spacing;
		advect_pc.max_tracers  = MAX_TRACERS;
		advect_pc.dt           = last_dt;
		advect_pc.min_life     = tracer_params.min_life;        // Part07涡环: FLT_MAX
		advect_pc.max_life     = tracer_params.max_life;        // Part07涡环: FLT_MAX

		tracer_advect_pass->bind_buffer("VelGrid", *vel_grid)
		    .bind_buffer("TracerPosIn", *tracer_pos[tracer_dst])
		    .bind_buffer("TracerAgeIn", *tracer_age[tracer_dst])
		    .bind_buffer("TracerPosOut", *tracer_pos[tracer_src])
		    .bind_buffer("TracerAgeOut", *tracer_age[tracer_src])
		    .bind_buffer("AliveCount", *tracer_alive_count)
		    .set_push_constants(advect_pc);
		tracer_advect_pass->draw(command_buffer);

		compute_barrier(*tracer_pos[tracer_src]);
		compute_barrier(*tracer_age[tracer_src]);

		// Clear tracer_write_counter to 0 before running compaction pass
		vkCmdFillBuffer(command_buffer.get_handle(), tracer_write_counter->get_handle(), 0, sizeof(uint32_t), 0);

		vkb::common::HPPBufferMemoryBarrier clear_write_counter_barrier{};
		clear_write_counter_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eTransfer;
		clear_write_counter_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		clear_write_counter_barrier.src_access_mask = vk::AccessFlagBits::eTransferWrite;
		clear_write_counter_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		command_buffer.buffer_memory_barrier(*tracer_write_counter, 0, VK_WHOLE_SIZE, clear_write_counter_barrier);

		// Compact dead tracers: tracer_src -> tracer_dst
		TracerCompactPC compact_pc{};
		compact_pc.max_tracers = MAX_TRACERS;

		tracer_compact_pass->bind_buffer("TracerPosIn", *tracer_pos[tracer_src])
		    .bind_buffer("TracerAgeIn", *tracer_age[tracer_src])
		    .bind_buffer("TracerPosOut", *tracer_pos[tracer_dst])
		    .bind_buffer("TracerAgeOut", *tracer_age[tracer_dst])
		    .bind_buffer("AliveCountIn", *tracer_alive_count)
		    .bind_buffer("AliveCountOut", *tracer_alive_count)
		    .bind_buffer("WriteCounter", *tracer_write_counter)
		    .set_push_constants(compact_pc);
		tracer_compact_pass->draw(command_buffer);

		compute_barrier(*tracer_pos[tracer_dst]);
		compute_barrier(*tracer_age[tracer_dst]);

		// Barrier to make compute writes to tracer_write_counter visible to transfer copy
		vkb::common::HPPBufferMemoryBarrier compact_to_transfer_barrier{};
		compact_to_transfer_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		compact_to_transfer_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eTransfer;
		compact_to_transfer_barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		compact_to_transfer_barrier.dst_access_mask = vk::AccessFlagBits::eTransferRead;
		command_buffer.buffer_memory_barrier(*tracer_write_counter, 0, VK_WHOLE_SIZE, compact_to_transfer_barrier);

		// Copy the compacted write count to tracer_alive_count
		command_buffer.copy_buffer(*tracer_write_counter, *tracer_alive_count, sizeof(uint32_t));

		// Barrier to make transfer copy visible to next frame's compute dispatches
		vkb::common::HPPBufferMemoryBarrier transfer_to_compute_barrier{};
		transfer_to_compute_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eTransfer;
		transfer_to_compute_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		transfer_to_compute_barrier.src_access_mask = vk::AccessFlagBits::eTransferWrite;
		transfer_to_compute_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		command_buffer.buffer_memory_barrier(*tracer_alive_count, 0, VK_WHOLE_SIZE, transfer_to_compute_barrier);

		tracer_current_buf = tracer_dst;        // Final output in tracer_dst
		tracer_emit_frame++;

		// First frame: GPU initializes all tracers and sets alive_count = MAX_TRACERS
		if (tracer_emit_frame == 1)
		{
			tracer_alive_count_value = MAX_TRACERS;
		}
		else
		{
			tracer_alive_count_value = std::min(tracer_alive_count_value + static_cast<uint32_t>(50000.0f * last_dt), MAX_TRACERS);
		}
		tracer_alive_count_value = std::max(1u, tracer_alive_count_value);

		vkb::common::HPPBufferMemoryBarrier vert_barrier{};
		vert_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		vert_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
		vert_barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		vert_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;

		command_buffer.buffer_memory_barrier(*particle_pos[dst], 0, VK_WHOLE_SIZE, vert_barrier);
		command_buffer.buffer_memory_barrier(*particle_vort[dst], 0, VK_WHOLE_SIZE, vert_barrier);
		command_buffer.buffer_memory_barrier(*tracer_pos[tracer_current_buf], 0, VK_WHOLE_SIZE, vert_barrier);
		command_buffer.buffer_memory_barrier(*tracer_age[tracer_current_buf], 0, VK_WHOLE_SIZE, vert_barrier);

		current_buf = dst;
	}        // end if (!debug_init_only)

	// Copy to staging buffer every 60 frames for diagnostics
	{
		static uint32_t copy_counter = 0;
		copy_counter++;
		if (copy_counter % 60 == 0)
		{
			VkBufferCopy copy_pos{};
			copy_pos.srcOffset = 0;
			copy_pos.dstOffset = 0;
			copy_pos.size = sizeof(glm::vec4);

			VkBufferCopy copy_vort{};
			copy_vort.srcOffset = 0;
			copy_vort.dstOffset = sizeof(glm::vec4);
			copy_vort.size = sizeof(glm::vec4);

			vkCmdCopyBuffer(
			    static_cast<VkCommandBuffer>(command_buffer.get_handle()),
			    static_cast<VkBuffer>(particle_pos[current_buf]->get_handle()),
			    static_cast<VkBuffer>(debug_staging_buffer->get_handle()),
			    1, &copy_pos
			);
			vkCmdCopyBuffer(
			    static_cast<VkCommandBuffer>(command_buffer.get_handle()),
			    static_cast<VkBuffer>(particle_vort[current_buf]->get_handle()),
			    static_cast<VkBuffer>(debug_staging_buffer->get_handle()),
			    1, &copy_vort
			);
		}
	}

	{
		vkb::common::HPPImageMemoryBarrier img_barrier{};
		img_barrier.old_layout      = vk::ImageLayout::eUndefined;
		img_barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		img_barrier.src_access_mask = {};
		img_barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		command_buffer.image_memory_barrier(views[Offscreen], img_barrier);
		render_target.set_layout(Offscreen, img_barrier.new_layout);

		command_buffer.bind_pipeline_layout(*offscreen_pipeline_layout);

		vkb::rendering::HPPVertexInputState vertex_input{};
		vertex_input.bindings   = {};
		vertex_input.attributes = {};
		command_buffer.set_vertex_input_state(vertex_input);
		command_buffer.set_input_assembly_state({vk::PrimitiveTopology::ePointList, VK_FALSE});

		vkb::rendering::HPPRasterizationState raster{};
		raster.cull_mode  = vk::CullModeFlagBits::eNone;
		raster.front_face = vk::FrontFace::eCounterClockwise;
		command_buffer.set_rasterization_state(raster);
		command_buffer.set_multisample_state({vk::SampleCountFlagBits::e1});
		command_buffer.set_depth_stencil_state({false, false, vk::CompareOp::eAlways});

		vkb::rendering::HPPColorBlendAttachmentState blend_attachment{};
		blend_attachment.blend_enable           = VK_TRUE;
		blend_attachment.src_color_blend_factor = vk::BlendFactor::eOne;
		blend_attachment.dst_color_blend_factor = vk::BlendFactor::eOne;
		blend_attachment.color_blend_op         = vk::BlendOp::eAdd;
		blend_attachment.src_alpha_blend_factor = vk::BlendFactor::eOne;
		blend_attachment.dst_alpha_blend_factor = vk::BlendFactor::eOne;
		blend_attachment.alpha_blend_op         = vk::BlendOp::eAdd;

		vkb::rendering::HPPColorBlendAttachmentState no_blend{};
		vkb::rendering::HPPColorBlendState           blend{};
		blend.attachments = {no_blend, blend_attachment};
		command_buffer.set_color_blend_state(blend);

		// Barrier for vertex shader access (must be BEFORE begin_render_pass)
		vkb::common::HPPBufferMemoryBarrier vorton_vert_barrier{};
		vorton_vert_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eComputeShader;
		vorton_vert_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
		vorton_vert_barrier.src_access_mask = vk::AccessFlagBits::eShaderWrite;
		vorton_vert_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
		command_buffer.buffer_memory_barrier(*particle_pos[current_buf], 0, VK_WHOLE_SIZE, vorton_vert_barrier);

		float     aspect     = static_cast<float>(extent.width) / static_cast<float>(extent.height);
		glm::mat4 projection = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 100.0f);

		glm::vec3 camera_pos = camera.target + glm::vec3(
		                                           cos(camera.pitch) * cos(camera.yaw),
		                                           sin(camera.pitch),
		                                           cos(camera.pitch) * sin(camera.yaw)) *
		                                           camera.distance;
		glm::mat4 view       = glm::lookAt(camera_pos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));

		TracerRenderPC render_pc{};
		render_pc.view_projection = projection * view;
		render_pc.physical_radius = 0.01f;        // 缩小5倍（原0.05f）
		render_pc.viewport_height = static_cast<float>(extent.height);
		render_pc.fov_y           = glm::radians(camera.fov);
		render_pc.max_life        = tracer_params.max_life;
		render_pc.young_color     = glm::vec3(1.0f, 1.0f, 1.0f);        // White for tracer
		render_pc.old_color       = glm::vec3(0.7f, 0.7f, 0.7f);        // Gray-white for old tracer

		auto                       &fb = cache.request_framebuffer(render_target, *offscreen_render_pass);
		std::vector<vk::ClearValue> clear_values(AttachmentCount, vk::ClearValue{});
		clear_values[Offscreen] = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
		command_buffer.begin_render_pass(render_target, *offscreen_render_pass, fb, clear_values);

		set_viewport_and_scissor(command_buffer, extent);

		vkb::rendering::HPPVertexInputState vorton_vertex_input{};
		vorton_vertex_input.bindings = {
		    {0, sizeof(glm::vec4), vk::VertexInputRate::eVertex},
		};
		vorton_vertex_input.attributes = {
		    {0, 0, vk::Format::eR32G32B32A32Sfloat, 0},
		};
		command_buffer.set_vertex_input_state(vorton_vertex_input);

		// Render vortons (debug mode) or tracers (normal mode)
		if (debug_init_only)
		{
			// Debug: render vorton particles only
			command_buffer.push_constants(render_pc);
			command_buffer.bind_vertex_buffers(0, {*particle_pos[current_buf]}, {0});
			command_buffer.draw(PARTICLE_COUNT, 1, 0, 0);
		}
		else
		{
			// Normal mode: render tracers and optionally vortons
			if (render_tracers)
			{
				command_buffer.push_constants(render_pc);
				command_buffer.bind_vertex_buffers(0, {*tracer_pos[tracer_current_buf]}, {0});
				command_buffer.draw(tracer_alive_count_value, 1, 0, 0);
			}

			if (render_vortons)
			{
				// Render vortons in blue color
				TracerRenderPC vorton_render_pc  = render_pc;
				vorton_render_pc.physical_radius = 0.004f;                             // 缩小5倍（原0.02f）
				vorton_render_pc.young_color     = glm::vec3(0.2f, 0.4f, 1.0f);        // Bright blue
				vorton_render_pc.old_color       = glm::vec3(0.1f, 0.2f, 0.5f);        // Dim blue
				command_buffer.push_constants(vorton_render_pc);

				command_buffer.bind_vertex_buffers(0, {*particle_pos[current_buf]}, {0});
				command_buffer.draw(PARTICLE_COUNT, 1, 0, 0);
			}
		}

		command_buffer.end_render_pass();
	}

	{
		if (render_target.get_layout(Offscreen) != vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			vkb::common::HPPImageMemoryBarrier img_barrier{};
			img_barrier.old_layout      = vk::ImageLayout::eColorAttachmentOptimal;
			img_barrier.new_layout      = vk::ImageLayout::eShaderReadOnlyOptimal;
			img_barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
			img_barrier.dst_access_mask = vk::AccessFlagBits::eInputAttachmentRead;
			img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eFragmentShader;
			command_buffer.image_memory_barrier(views[Offscreen], img_barrier);
			render_target.set_layout(Offscreen, img_barrier.new_layout);
		}

		if (render_target.get_layout(Swapchain) != vk::ImageLayout::eColorAttachmentOptimal)
		{
			vkb::common::HPPImageMemoryBarrier img_barrier{};
			img_barrier.old_layout      = render_target.get_layout(Swapchain);
			img_barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
			img_barrier.src_access_mask = {};
			img_barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
			img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			command_buffer.image_memory_barrier(views[Swapchain], img_barrier);
			render_target.set_layout(Swapchain, img_barrier.new_layout);
		}

		command_buffer.bind_pipeline_layout(*copy_pipeline_layout);

		command_buffer.bind_image(views[Offscreen], *copy_sampler, 0, 0, 0);

		vkb::rendering::HPPVertexInputState vertex_input{};
		vertex_input.bindings   = {};
		vertex_input.attributes = {};
		command_buffer.set_vertex_input_state(vertex_input);
		command_buffer.set_input_assembly_state({vk::PrimitiveTopology::eTriangleList, VK_FALSE});

		vkb::rendering::HPPRasterizationState raster{};
		raster.cull_mode = vk::CullModeFlagBits::eNone;
		command_buffer.set_rasterization_state(raster);
		command_buffer.set_multisample_state({vk::SampleCountFlagBits::e1});
		command_buffer.set_depth_stencil_state({false, false, vk::CompareOp::eAlways});

		vkb::rendering::HPPColorBlendAttachmentState no_blend{};
		vkb::rendering::HPPColorBlendState           blend{};
		blend.attachments = {no_blend, no_blend};
		command_buffer.set_color_blend_state(blend);

		auto                       &fb = cache.request_framebuffer(render_target, *copy_render_pass);
		std::vector<vk::ClearValue> clear_values(AttachmentCount, vk::ClearValue{});
		command_buffer.begin_render_pass(render_target, *copy_render_pass, fb, clear_values);

		set_viewport_and_scissor(command_buffer, extent);
		command_buffer.draw(3, 1, 0, 0);

		if (has_gui())
		{
			get_gui().draw(command_buffer);
		}

		command_buffer.end_render_pass();
	}

	{
		vkb::common::HPPImageMemoryBarrier img_barrier{};
		img_barrier.old_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		img_barrier.new_layout      = vk::ImageLayout::ePresentSrcKHR;
		img_barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		img_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		img_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
		command_buffer.image_memory_barrier(views[Swapchain], img_barrier);
		render_target.set_layout(Swapchain, img_barrier.new_layout);
	}
}

void VortexRing::input_event(const vkb::InputEvent &event)
{
	vkb::VulkanSampleCpp::input_event(event);

	if (event.get_source() == vkb::EventSource::Mouse)
	{
		const auto &mouse_event = static_cast<const vkb::MouseButtonInputEvent &>(event);

		switch (mouse_event.get_action())
		{
			case vkb::MouseAction::Down:
			{
				mouse_dragging = true;
				last_mouse_pos = glm::vec2(mouse_event.get_pos_x(), mouse_event.get_pos_y());
				break;
			}

			case vkb::MouseAction::Up:
			{
				mouse_dragging = false;
				break;
			}

			case vkb::MouseAction::Move:
			{
				if (mouse_dragging)
				{
					glm::vec2 current_pos(mouse_event.get_pos_x(), mouse_event.get_pos_y());
					glm::vec2 delta = current_pos - last_mouse_pos;
					last_mouse_pos  = current_pos;

					if (mouse_event.get_button() == vkb::MouseButton::Left)
					{
						camera.yaw -= delta.x * 0.005f;
						camera.pitch += delta.y * 0.005f;
						camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);
					}
					else if (mouse_event.get_button() == vkb::MouseButton::Right)
					{
						camera.distance -= delta.y * 0.01f;
						camera.distance = glm::clamp(camera.distance, 2.0f, 50.0f);
					}
				}
				break;
			}

			default:
				break;
		}
	}
}

void VortexRing::draw_gui()
{
	auto &drawer = get_gui().get_drawer();

	if (drawer.header("Vortex Ring"))
	{
		drawer.slider_float("Ring Radius", &vortex_params.ring_radius, 0.1f, 5.0f);
		drawer.slider_float("Thickness", &vortex_params.thickness, 0.01f, 1.0f);
		drawer.slider_float("Circulation", &vortex_params.circulation, 0.1f, 200.0f);
		drawer.slider_float("Core Radius", &vortex_params.core_radius, 0.01f, 0.5f);
		drawer.slider_float("Viscosity", &vortex_params.viscosity, 0.0f, 0.1f);
		drawer.slider_float("Stretch Factor", &vortex_params.stretch_factor, 0.0f, 0.5f);

		drawer.text("Particle Distribution:");
		drawer.slider_int("Ring Segments", reinterpret_cast<int *>(&vortex_params.ring_segments), 4, 32);
		drawer.slider_int("Tube Rings", reinterpret_cast<int *>(&vortex_params.tube_rings), 4, 32);
		drawer.slider_int("Tube Radial", reinterpret_cast<int *>(&vortex_params.tube_radial), 4, 32);
		drawer.text("Total: %d (= %d)",
		            vortex_params.ring_segments * vortex_params.tube_rings * vortex_params.tube_radial,
		            PARTICLE_COUNT);

		drawer.checkbox("Debug Init Only", &debug_init_only);
		if (!debug_init_only)
		{
			drawer.checkbox("Render Tracers", &render_tracers);
			drawer.checkbox("Render Vortons", &render_vortons);
			drawer.text("Alive Tracers: %d", tracer_alive_count_value);
		}
		if (drawer.button("Reinitialize"))
		{
			initialized              = false;
			tracer_emit_frame        = 0;
			tracer_current_buf       = 0;
			current_buf              = 0;
			tracer_alive_count_value = 0;
			elapsed                  = 0.0f;
		}

		drawer.text("FPS: %.1f", fps);
	}

	if (drawer.header("Camera"))
	{
		drawer.slider_float("Distance", &camera.distance, 2.0f, 50.0f);
		drawer.slider_float("FOV", &camera.fov, 30.0f, 120.0f);
		drawer.text("Yaw: %.1f", camera.yaw);
		drawer.text("Pitch: %.1f", camera.pitch);
		drawer.text("Left-drag: Rotate | Right-drag: Zoom");
	}
}

std::unique_ptr<vkb::Application> create_vortex_ring()
{
	return std::make_unique<VortexRing>();
}