#include "gpu_particle_effects.h"

#include "gui.h"
#include "scene_graph/components/camera.h"

GpuParticleEffects::ParticleSubpass::ParticleSubpass(vkb::rendering::RenderContextC &render_context, vkb::ShaderSource &&vertex_shader, vkb::ShaderSource &&fragment_shader) :
    vkb::rendering::SubpassC(render_context, std::move(vertex_shader), std::move(fragment_shader))
{
}

void GpuParticleEffects::ParticleSubpass::prepare()
{
	auto &resource_cache = get_render_context().get_device().get_resource_cache();
	auto &vert_module    = resource_cache.request_shader_module(VK_SHADER_STAGE_VERTEX_BIT, get_vertex_shader());
	auto &frag_module    = resource_cache.request_shader_module(VK_SHADER_STAGE_FRAGMENT_BIT, get_fragment_shader());
	auto &pipeline_layout = resource_cache.request_pipeline_layout({&vert_module, &frag_module});
	sample->graphics.pipeline_layout = &pipeline_layout;
}

void GpuParticleEffects::ParticleSubpass::draw(vkb::core::CommandBufferC &command_buffer)
{
	command_buffer.bind_pipeline_layout(*sample->graphics.pipeline_layout);
	
	vkb::VertexInputState vertex_input_state;
	vertex_input_state.attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, position)});
	vertex_input_state.attributes.push_back({1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, velocity)});
	vertex_input_state.attributes.push_back({2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, color)});
	vertex_input_state.attributes.push_back({3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, misc)});
	vertex_input_state.bindings.push_back({0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX});
	command_buffer.set_vertex_input_state(vertex_input_state);

	command_buffer.set_input_assembly_state({VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_FALSE});
	
	vkb::ColorBlendAttachmentState blend_attachment{};
	blend_attachment.blend_enable = VK_TRUE;
	blend_attachment.color_blend_op = VK_BLEND_OP_ADD;
	blend_attachment.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
	blend_attachment.alpha_blend_op = VK_BLEND_OP_ADD;
	blend_attachment.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
	blend_attachment.color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	vkb::ColorBlendState color_blend_state;
	color_blend_state.attachments.push_back(blend_attachment);
	command_buffer.set_color_blend_state(color_blend_state);

	command_buffer.set_depth_stencil_state({VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS});
	command_buffer.set_rasterization_state({VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE});

	command_buffer.bind_buffer(*sample->graphics.uniform_buffer, 0, sample->graphics.uniform_buffer->get_size(), 0, 0, 0); // binding 0

	std::vector<std::reference_wrapper<const vkb::core::BufferC>> buffers = {std::cref(*sample->compute.storage_buffer)};
	command_buffer.bind_vertex_buffers(0, buffers, {0});
	
	command_buffer.draw(sample->num_particles, 1, 0, 0);
}

GpuParticleEffects::GpuParticleEffects()
{
}

GpuParticleEffects::~GpuParticleEffects() = default;

void GpuParticleEffects::request_gpu_features(vkb::core::PhysicalDeviceC &gpu)
{
	if (gpu.get_features().shaderInt64)
	{
		gpu.get_mutable_requested_features().shaderInt64 = VK_TRUE;
	}
}

void GpuParticleEffects::prepare_storage_buffers()
{
	std::vector<Particle> particle_buffer(num_particles);
	for (auto &p : particle_buffer)
	{
		p.position = glm::vec4(0.0f);
		p.velocity = glm::vec4(0.0f);
		p.color    = glm::vec4(1.0f);
		p.misc     = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); // default size=1.0
	}

	VkDeviceSize storage_buffer_size = particle_buffer.size() * sizeof(Particle);
	vkb::core::BufferC staging_buffer = vkb::core::BufferC::create_staging_buffer(get_device(), particle_buffer);
	compute.storage_buffer = std::make_unique<vkb::core::BufferC>(get_device(),
	                                                                storage_buffer_size,
	                                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                                VMA_MEMORY_USAGE_GPU_ONLY);

	std::vector<uint32_t> dead_indices(num_particles);
	for (uint32_t i = 0; i < num_particles; ++i)
	{
		dead_indices[i] = i;
	}

	compute.dead_indices_buffer = std::make_unique<vkb::core::BufferC>(get_device(),
	                                                                  num_particles * sizeof(uint32_t),
	                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                                  VMA_MEMORY_USAGE_GPU_ONLY);

	vkb::core::BufferC indices_staging = vkb::core::BufferC::create_staging_buffer(get_device(), dead_indices);

	VkCommandBuffer copy_command = get_device().create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	
	VkBufferCopy copy_region = {0, 0, storage_buffer_size};
	vkCmdCopyBuffer(copy_command, staging_buffer.get_handle(), compute.storage_buffer->get_handle(), 1, &copy_region);
	
	VkBufferCopy indices_copy_region = {0, 0, num_particles * sizeof(uint32_t)};
	vkCmdCopyBuffer(copy_command, indices_staging.get_handle(), compute.dead_indices_buffer->get_handle(), 1, &indices_copy_region);

	get_device().flush_command_buffer(copy_command, get_device().get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0).get_handle(), true);

	uint32_t initial_dead_count = num_particles;
	compute.dead_count_buffer = std::make_unique<vkb::core::BufferC>(get_device(),
	                                                                4,
	                                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
	compute.dead_count_buffer->convert_and_update(initial_dead_count);
}

void GpuParticleEffects::prepare_uniform_buffers()
{
	compute.uniform_buffer = std::make_unique<vkb::core::BufferC>(get_device(),
	                                                                sizeof(EmitterParams),
	                                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                                                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	graphics.uniform_buffer = std::make_unique<vkb::core::BufferC>(get_device(),
	                                                                sizeof(GraphicsUBO),
	                                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                                                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	update_compute_uniform_buffers(1.0f);
	update_graphics_uniform_buffers();
}

void GpuParticleEffects::update_compute_uniform_buffers(float delta_time)
{
	compute.ubo.time += delta_time;
	compute.ubo.delta_time = delta_time;
	compute.ubo.particle_count = num_particles;
	compute.uniform_buffer->convert_and_update(compute.ubo);
}

void GpuParticleEffects::update_graphics_uniform_buffers()
{
	auto extent = window->get_extent();
	graphics.ubo.projection    = camera.matrices.perspective;
	graphics.ubo.view          = camera.matrices.view;
	graphics.ubo.screen_dim    = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
	graphics.ubo.particle_size = compute.ubo.particle_size;
	graphics.uniform_buffer->convert_and_update(graphics.ubo);
}

bool GpuParticleEffects::prepare(const vkb::ApplicationOptions &options)
{
	if (!vkb::VulkanSampleC::prepare(options))
	{
		return false;
	}

	set_name("GPU Particle Effects");

	// Setup camera
	auto extent = window->get_extent();
	camera.type = vkb::CameraType::LookAt;
	camera.set_position(glm::vec3(0.0f, 2.0f, -10.0f));
	camera.set_rotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.set_perspective(60.0f, static_cast<float>(extent.width) / static_cast<float>(extent.height), 0.1f, 256.0f);

	// Default emitter parameters
	compute.ubo.position       = glm::vec3(0.0f, 0.0f, 0.0f);
	compute.ubo.emit_rate      = 5000.0f;
	compute.ubo.direction      = glm::vec3(0.0f, 1.0f, 0.0f);
	compute.ubo.cone_angle     = 0.5f;
	compute.ubo.gravity        = glm::vec3(0.0f, -9.81f, 0.0f);
	compute.ubo.min_life       = 1.0f;
	compute.ubo.max_life       = 3.0f;
	compute.ubo.min_speed      = 1.0f;
	compute.ubo.max_speed      = 5.0f;
	compute.ubo.radius         = 1.0f;
	compute.ubo.particle_size  = 2.0f;
	compute.ubo.emitter_type   = 0;
	compute.ubo.time           = 0.0f;
	compute.ubo.wind_strength  = 1.0f;
	compute.ubo.wind_direction = glm::vec3(1.0f, 0.1f, 0.0f);
	compute.ubo.extent         = glm::vec3(2.0f, 2.0f, 2.0f);
	compute.ubo.particle_count = num_particles;
	compute.ubo.seed           = 42;

	prepare_storage_buffers();
	prepare_uniform_buffers();

	work_group_size = std::min(static_cast<uint32_t>(128), get_device().get_gpu().get_properties().limits.maxComputeWorkGroupSize[0]);

	auto subpass = std::make_unique<ParticleSubpass>(get_render_context(),
	                                                 vkb::ShaderSource{"gpu_particle_effects/glsl/particle.vert.spv"},
	                                                 vkb::ShaderSource{"gpu_particle_effects/glsl/particle.frag.spv"});
	subpass->sample = this;

	auto render_pipeline = std::make_unique<vkb::rendering::RenderPipelineC>();
	render_pipeline->add_subpass(std::move(subpass));

	set_render_pipeline(std::move(render_pipeline));

	auto &resource_cache = get_device().get_resource_cache();
	
	vkb::ShaderSource emit_source{"gpu_particle_effects/glsl/particle_emit.comp.spv"};
	vkb::ShaderModule &emit_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, emit_source);
	std::vector<vkb::ShaderModule *> emit_shaders = {&emit_module};
	compute.pipeline_layout_emit = &resource_cache.request_pipeline_layout(emit_shaders);

	vkb::ShaderSource update_source{"gpu_particle_effects/glsl/particle_update.comp.spv"};
	vkb::ShaderModule &update_module = resource_cache.request_shader_module(VK_SHADER_STAGE_COMPUTE_BIT, update_source);
	std::vector<vkb::ShaderModule *> update_shaders = {&update_module};
	compute.pipeline_layout_update = &resource_cache.request_pipeline_layout(update_shaders);

	get_render_pipeline().prepare();

	return true;
}

void GpuParticleEffects::update(float delta_time)
{
	camera.update(delta_time);
	update_compute_uniform_buffers(delta_time);
	if (camera.updated)
	{
		update_graphics_uniform_buffers();
	}

	vkb::VulkanSampleC::update(delta_time);
}

void GpuParticleEffects::draw(vkb::core::CommandBufferC &command_buffer, vkb::rendering::RenderTargetC &render_target)
{
	// Bind buffers for compute
	command_buffer.bind_buffer(*compute.storage_buffer, 0, compute.storage_buffer->get_size(), 0, 0, 0);
	command_buffer.bind_buffer(*compute.uniform_buffer, 0, compute.uniform_buffer->get_size(), 0, 1, 0);
	command_buffer.bind_buffer(*compute.dead_count_buffer, 0, compute.dead_count_buffer->get_size(), 0, 2, 0);
	command_buffer.bind_buffer(*compute.dead_indices_buffer, 0, compute.dead_indices_buffer->get_size(), 0, 3, 0);

	// Pass 1: Emit
	command_buffer.bind_pipeline_layout(*compute.pipeline_layout_emit);
	command_buffer.dispatch(4, 1, 1);

	// Barrier between emit and update
	vkb::BufferMemoryBarrier emit_barrier{};
	emit_barrier.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	emit_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	emit_barrier.src_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
	emit_barrier.dst_access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	command_buffer.buffer_memory_barrier(*compute.storage_buffer, 0, compute.storage_buffer->get_size(), emit_barrier);

	// Pass 2: Update
	command_buffer.bind_pipeline_layout(*compute.pipeline_layout_update);
	command_buffer.dispatch((num_particles + work_group_size - 1) / work_group_size, 1, 1);

	// Barrier between update and graphics
	vkb::BufferMemoryBarrier compute_barrier{};
	compute_barrier.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	compute_barrier.dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	compute_barrier.src_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
	compute_barrier.dst_access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	command_buffer.buffer_memory_barrier(*compute.storage_buffer, 0, compute.storage_buffer->get_size(), compute_barrier);

	// Default frame rendering
	vkb::VulkanSampleC::draw(command_buffer, render_target);
}

void GpuParticleEffects::reset_particles()
{
	std::vector<Particle> particle_buffer(num_particles);
	for (auto &p : particle_buffer)
	{
		p.position = glm::vec4(0.0f);
		p.velocity = glm::vec4(0.0f);
		p.color    = glm::vec4(1.0f);
		p.misc     = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	}

	VkDeviceSize storage_buffer_size = particle_buffer.size() * sizeof(Particle);
	vkb::core::BufferC staging_buffer = vkb::core::BufferC::create_staging_buffer(get_device(), particle_buffer);

	std::vector<uint32_t> dead_indices(num_particles);
	for (uint32_t i = 0; i < num_particles; ++i)
	{
		dead_indices[i] = i;
	}
	vkb::core::BufferC indices_staging = vkb::core::BufferC::create_staging_buffer(get_device(), dead_indices);

	VkCommandBuffer copy_command = get_device().create_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	
	VkBufferCopy copy_region = {0, 0, storage_buffer_size};
	vkCmdCopyBuffer(copy_command, staging_buffer.get_handle(), compute.storage_buffer->get_handle(), 1, &copy_region);
	
	VkBufferCopy indices_copy_region = {0, 0, num_particles * sizeof(uint32_t)};
	vkCmdCopyBuffer(copy_command, indices_staging.get_handle(), compute.dead_indices_buffer->get_handle(), 1, &indices_copy_region);

	get_device().flush_command_buffer(copy_command, get_device().get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0).get_handle(), true);

	compute.ubo.seed++;
	compute.ubo.time = 0.0f;

	uint32_t initial_dead_count = num_particles;
	compute.dead_count_buffer->convert_and_update(initial_dead_count);
}

void GpuParticleEffects::draw_gui()
{
	auto &drawer = get_gui().get_drawer();
	std::vector<std::string> emitter_types = {"Point", "Sphere", "Cone", "Box"};

	if (drawer.header("Emitter"))
	{
		drawer.combo_box("Type", &compute.ubo.emitter_type, emitter_types);
		drawer.slider_float("Emit Rate", &compute.ubo.emit_rate, 100.0f, 50000.0f);
		drawer.slider_float("Radius", &compute.ubo.radius, 0.0f, 50.0f);
		drawer.input_float("Position X", &compute.ubo.position.x, 0.1f, "%.2f");
		drawer.input_float("Position Y", &compute.ubo.position.y, 0.1f, "%.2f");
		drawer.input_float("Position Z", &compute.ubo.position.z, 0.1f, "%.2f");
		drawer.input_float("Direction X", &compute.ubo.direction.x, 0.1f, "%.2f");
		drawer.input_float("Direction Y", &compute.ubo.direction.y, 0.1f, "%.2f");
		drawer.input_float("Direction Z", &compute.ubo.direction.z, 0.1f, "%.2f");
		drawer.slider_float("Cone Angle", &compute.ubo.cone_angle, 0.01f, 3.14f);
		drawer.slider_float("Extent X", &compute.ubo.extent.x, 0.0f, 50.0f);
		drawer.slider_float("Extent Y", &compute.ubo.extent.y, 0.0f, 50.0f);
		drawer.slider_float("Extent Z", &compute.ubo.extent.z, 0.0f, 50.0f);
		drawer.input_float("Gravity X", &compute.ubo.gravity.x, 0.1f, "%.2f");
		drawer.input_float("Gravity Y", &compute.ubo.gravity.y, 0.1f, "%.2f");
		drawer.input_float("Gravity Z", &compute.ubo.gravity.z, 0.1f, "%.2f");
	}
	if (drawer.header("Particle"))
	{
		drawer.slider_float("Size", &compute.ubo.particle_size, 0.1f, 10.0f);
		drawer.slider_float("Wind Strength", &compute.ubo.wind_strength, 0.0f, 10.0f);
		drawer.slider_float("Min Life", &compute.ubo.min_life, 0.1f, 10.0f);
		drawer.slider_float("Max Life", &compute.ubo.max_life, 0.1f, 10.0f);
		drawer.slider_float("Min Speed", &compute.ubo.min_speed, 0.1f, 20.0f);
		drawer.slider_float("Max Speed", &compute.ubo.max_speed, 0.1f, 20.0f);
		drawer.text("Particles: %d", num_particles);
	}
	if (drawer.header("Controls"))
	{
		if (drawer.button("Reset Particles"))
		{
			reset_particles();
		}
	}
}

std::unique_ptr<vkb::Application> create_gpu_particle_effects()
{
	return std::make_unique<GpuParticleEffects>();
}
