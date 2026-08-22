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

/*
 * Ring Particles Sample
 *
 * Instanced particles distributed on a ring. Each instance's position is
 * computed procedurally in the vertex shader from gl_InstanceID, so no
 * instance vertex buffer is needed. The user-supplied formula:
 *
 *   angle          = (instanceId % ringParticleNum) / ringParticleNum * 2*PI
 *   positionOffset = (cos(angle), sin(angle), 0)
 *   positionOffset *= ringRadius * particleScale
 */

#include "ring_particles.h"

#include <cmath>

#include "common/vk_common.h"
#include "core/util/logging.hpp"

RingParticles::RingParticles()
{
	title = "Ring Particles";
	zoom  = -6.0f;
}

RingParticles::~RingParticles()
{
	if (has_device())
	{
		vkDestroyPipeline(get_device().get_handle(), pipeline, nullptr);
		vkDestroyPipelineLayout(get_device().get_handle(), pipeline_layout, nullptr);
		vkDestroyDescriptorSetLayout(get_device().get_handle(), descriptor_set_layout, nullptr);
		vkDestroyDescriptorPool(get_device().get_handle(), descriptor_pool, nullptr);
		uniform_buffer.reset();

		// Post-process resources.
		vkDestroyPipeline(get_device().get_handle(), post_pipeline, nullptr);
		vkDestroyPipelineLayout(get_device().get_handle(), post_pipeline_layout, nullptr);
		vkDestroyDescriptorSetLayout(get_device().get_handle(), post_descriptor_set_layout, nullptr);
		post_uniform_buffer.reset();
		vkDestroySampler(get_device().get_handle(), offscreen_sampler, nullptr);
		destroy_offscreen_resources();
		vkDestroyRenderPass(get_device().get_handle(), offscreen_render_pass, nullptr);
	}
}

bool RingParticles::prepare(const vkb::ApplicationOptions &options)
{
	if (!ApiVulkanSample::prepare(options))
	{
		return false;
	}

	camera.type = vkb::CameraType::FirstPerson;
	camera.set_perspective(60.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);
	// Camera looks straight down -Z so the XY-plane ring renders as a circle
	// (no Y foreshortening from a tilted view).
	camera.set_translation(glm::vec3(0.0f, 0.0f, -6.0f));
	camera.set_rotation(glm::vec3(0.0f, 0.0f, 0.0f));

	// Mirror the runtime particle count into the UBO.
	ubo.ringParticleNum        = particle_count;
	ubo.ringRadius             = ring_radius;
	ubo.particleScale           = particle_scale;
	ubo.iparticleSizeInstance   = iparticle_size_instance;
	ubo.locationFrequencies     = location_frequencies;
	ubo.timeFrequencies         = time_frequencies;
	ubo.displace                = displace;
	ubo.particlePositionNoise   = particle_position_noise;
	ubo.ringWidth1               = ring_width1;
	ubo.sizeRate                 = size_rate;
	ubo.particleReformNoise      = particle_reform_noise;
	ubo.pointSizeScale            = point_size_scale;

	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();

	// Post-process (rotation blur layered over the particle render).
	setup_post_descriptor_set_layout();
	prepare_post_pipelines();
	post_uniform_buffer = std::make_unique<vkb::core::BufferC>(
	    get_device(), sizeof(PostUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	setup_post_descriptor_set();
	update_post_uniform_buffer();

	build_command_buffers();

	prepared = true;
	return true;
}

void RingParticles::prepare_gui()
{
	// Enable frame-time stats so the overlay shows ms/frame + fps.
	get_stats().request_stats({vkb::StatIndex::frame_times});
	// Pass &get_stats() (base uses nullptr) so the GUI renders the stats panel.
	create_gui(*window, &get_stats(), 15.0f, true);

	std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
	    load_shader("uioverlay/uioverlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
	    load_shader("uioverlay/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)};

	get_gui().prepare(pipeline_cache, render_pass, shader_stages, get_gui_subpass());
}

void RingParticles::setup_render_pass()
{
	// Base swapchain render pass (presented). Kept for the post-process pass
	// that writes the final rotation-blur result to the swapchain.
	ApiVulkanSample::setup_render_pass();

	// Offscreen render pass is extent-independent; create it once.
	if (offscreen_render_pass != VK_NULL_HANDLE)
	{
		return;
	}
	// Offscreen render pass for the particle render. Its Color attachment
	// ends in SHADER_READ_ONLY_OPTIMAL so the blur pass can sample it.
	std::array<VkAttachmentDescription, 2> attachments{};
	attachments[0].format        = get_render_context().get_format();
	attachments[0].samples       = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp       = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp      = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[1].format        = depth_format;
	attachments[1].samples       = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp       = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount    = 1;
	subpass.pColorAttachments        = &color_ref;
	subpass.pDepthStencilAttachment = &depth_ref;

	VkSubpassDependency dep{};
	dep.srcSubpass      = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass      = 0;
	dep.srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dep.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep.srcAccessMask   = VK_ACCESS_NONE_KHR;
	dep.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo rp_info{};
	rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp_info.attachmentCount = static_cast<uint32_t>(attachments.size());
	rp_info.pAttachments    = attachments.data();
	rp_info.subpassCount    = 1;
	rp_info.pSubpasses     = &subpass;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies  = &dep;
	VK_CHECK(vkCreateRenderPass(get_device().get_handle(), &rp_info, nullptr, &offscreen_render_pass));
}

void RingParticles::setup_framebuffer()
{
	// Base swapchain framebuffers (used by the post-process pass).
	ApiVulkanSample::setup_framebuffer();
	// (Re)build the offscreen color/depth images + framebuffer to match.
	create_offscreen_resources();
	// The offscreen color view was (re)created; refresh the post descriptor so
	// it samples the new view. (No-op before the descriptor set is allocated.)
	if (post_descriptor_set != VK_NULL_HANDLE)
	{
		update_post_descriptor_writes();
	}
}

void RingParticles::create_offscreen_resources()
{
	auto &dev     = get_device().get_handle();
	auto  extent  = get_render_context().get_surface_extent();
	auto  color_fmt = get_render_context().get_format();

	// Tear down any previous offscreen resources (handles resize).
	destroy_offscreen_resources();

	auto create_image = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage *img, VkDeviceMemory *mem, VkImageView *view) {
		VkImageCreateInfo ici{};
		ici.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType   = VK_IMAGE_TYPE_2D;
		ici.format      = fmt;
		ici.extent      = {extent.width, extent.height, 1};
		ici.mipLevels   = 1;
		ici.arrayLayers = 1;
		ici.samples    = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling     = VK_IMAGE_TILING_OPTIMAL;
		ici.usage      = usage;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK(vkCreateImage(dev, &ici, nullptr, img));

		VkMemoryRequirements mem_reqs{};
		vkGetImageMemoryRequirements(dev, *img, &mem_reqs);
		VkMemoryAllocateInfo mai{};
		mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize   = mem_reqs.size;
		mai.memoryTypeIndex  = get_device().get_gpu().get_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vkAllocateMemory(dev, &mai, nullptr, mem));
		VK_CHECK(vkBindImageMemory(dev, *img, *mem, 0));

		VkImageViewCreateInfo vci{};
		vci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image      = *img;
		vci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
		vci.format     = fmt;
		vci.subresourceRange.aspectMask = (fmt == depth_format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.layerCount = 1;
		VK_CHECK(vkCreateImageView(dev, &vci, nullptr, view));
	};

	create_image(color_fmt,
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	             &offscreen_color_image, &offscreen_color_memory, &offscreen_color_view);
	create_image(depth_format,
	             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	             &offscreen_depth_image, &offscreen_depth_memory, &offscreen_depth_view);

	VkImageView attachments[2] = {offscreen_color_view, offscreen_depth_view};
	VkFramebufferCreateInfo fbi{};
	fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbi.renderPass       = offscreen_render_pass;
	fbi.attachmentCount  = 2;
	fbi.pAttachments     = attachments;
	fbi.width           = extent.width;
	fbi.height          = extent.height;
	fbi.layers          = 1;
	VK_CHECK(vkCreateFramebuffer(dev, &fbi, nullptr, &offscreen_framebuffer));

	// Linear sampler for the offscreen color (sRGB view handles decode).
	// Extent-independent; create once.
	if (offscreen_sampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo sci{};
		sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter    = VK_FILTER_LINEAR;
		sci.minFilter    = VK_FILTER_LINEAR;
		sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		VK_CHECK(vkCreateSampler(dev, &sci, nullptr, &offscreen_sampler));
	}

	offscreen_resources_created = true;
}

// Write (or refresh) the post descriptor set's bindings to point at the
// current offscreen color view + UBO. Called once after allocation and again
// on resize (the view is recreated, so the descriptor must be refreshed).
void RingParticles::update_post_descriptor_writes()
{
	VkDescriptorImageInfo img_info{};
	img_info.sampler     = offscreen_sampler;
	img_info.imageView   = offscreen_color_view;
	img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorBufferInfo buf_info = create_descriptor(*post_uniform_buffer);

	std::vector<VkWriteDescriptorSet> writes = {
	    vkb::initializers::write_descriptor_set(post_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &img_info),
	    vkb::initializers::write_descriptor_set(post_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &buf_info),
	};
	vkUpdateDescriptorSets(get_device().get_handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RingParticles::destroy_offscreen_resources()
{
	auto &dev = get_device().get_handle();
	if (offscreen_framebuffer)
	{
		vkDestroyFramebuffer(dev, offscreen_framebuffer, nullptr);
		offscreen_framebuffer = VK_NULL_HANDLE;
	}
	if (offscreen_color_view)
	{
		vkDestroyImageView(dev, offscreen_color_view, nullptr);
		offscreen_color_view = VK_NULL_HANDLE;
	}
	if (offscreen_color_image)
	{
		vkDestroyImage(dev, offscreen_color_image, nullptr);
		offscreen_color_image = VK_NULL_HANDLE;
	}
	if (offscreen_color_memory)
	{
		vkFreeMemory(dev, offscreen_color_memory, nullptr);
		offscreen_color_memory = VK_NULL_HANDLE;
	}
	if (offscreen_depth_view)
	{
		vkDestroyImageView(dev, offscreen_depth_view, nullptr);
		offscreen_depth_view = VK_NULL_HANDLE;
	}
	if (offscreen_depth_image)
	{
		vkDestroyImage(dev, offscreen_depth_image, nullptr);
		offscreen_depth_image = VK_NULL_HANDLE;
	}
	if (offscreen_depth_memory)
	{
		vkFreeMemory(dev, offscreen_depth_memory, nullptr);
		offscreen_depth_memory = VK_NULL_HANDLE;
	}
}

void RingParticles::setup_post_descriptor_set_layout()
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
	    vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
	    vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
	};
	VkDescriptorSetLayoutCreateInfo info =
	    vkb::initializers::descriptor_set_layout_create_info(bindings.data(), static_cast<uint32_t>(bindings.size()));
	VK_CHECK(vkCreateDescriptorSetLayout(get_device().get_handle(), &info, nullptr, &post_descriptor_set_layout));

	VkPipelineLayoutCreateInfo pl = vkb::initializers::pipeline_layout_create_info(&post_descriptor_set_layout, 1);
	VK_CHECK(vkCreatePipelineLayout(get_device().get_handle(), &pl, nullptr, &post_pipeline_layout));
}

void RingParticles::prepare_post_pipelines()
{
	VkPipelineVertexInputStateCreateInfo vertex_input =
	    vkb::initializers::pipeline_vertex_input_state_create_info();   // no vertex input

	VkPipelineInputAssemblyStateCreateInfo input_assembly =
	    vkb::initializers::pipeline_input_assembly_state_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterization =
	    vkb::initializers::pipeline_rasterization_state_create_info(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

	VkPipelineColorBlendAttachmentState blend =
	    vkb::initializers::pipeline_color_blend_attachment_state(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo color_blend =
	    vkb::initializers::pipeline_color_blend_state_create_info(1, &blend);

	VkPipelineDepthStencilStateCreateInfo depth_stencil =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewport = vkb::initializers::pipeline_viewport_state_create_info(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisample = vkb::initializers::pipeline_multisample_state_create_info(VK_SAMPLE_COUNT_1_BIT, 0);
	std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = vkb::initializers::pipeline_dynamic_state_create_info(dyn.data(), static_cast<uint32_t>(dyn.size()), 0);

	std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
	    load_shader("ring_particles", "fullscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
	    load_shader("ring_particles", "rotation_blur_post.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	VkGraphicsPipelineCreateInfo info = vkb::initializers::pipeline_create_info(post_pipeline_layout, render_pass, 0);
	info.pVertexInputState   = &vertex_input;
	info.pInputAssemblyState = &input_assembly;
	info.pRasterizationState = &rasterization;
	info.pColorBlendState    = &color_blend;
	info.pDepthStencilState  = &depth_stencil;
	info.pViewportState      = &viewport;
	info.pMultisampleState   = &multisample;
	info.pDynamicState       = &dynamic;
	info.stageCount          = static_cast<uint32_t>(stages.size());
	info.pStages              = stages.data();
	VK_CHECK(vkCreateGraphicsPipelines(get_device().get_handle(), pipeline_cache, 1, &info, nullptr, &post_pipeline));
}

void RingParticles::setup_post_descriptor_set()
{
	VkDescriptorSetAllocateInfo alloc =
	    vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &post_descriptor_set_layout, 1);
	VK_CHECK(vkAllocateDescriptorSets(get_device().get_handle(), &alloc, &post_descriptor_set));

	update_post_descriptor_writes();
}

RingParticles::PostUBO RingParticles::build_post_ubo() const
{
	PostUBO u{};
	u.center       = glm::vec2(rb_center[0], rb_center[1]);
	u.total_angle  = glm::radians(rb_sweep_degrees);
	u.samples      = enable_rotation_blur ? rb_samples : 1u;
	return u;
}

void RingParticles::update_post_uniform_buffer()
{
	post_ubo = build_post_ubo();
	post_uniform_buffer->convert_and_update(post_ubo);
}

void RingParticles::prepare_uniform_buffers()
{
	uniform_buffer = std::make_unique<vkb::core::BufferC>(
	    get_device(),
	    sizeof(UBO),
	    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);
	update_uniform_buffers(0.0f);
}

void RingParticles::update_uniform_buffers(float delta_time)
{
	if (rotate_ring)
	{
		ubo.time += delta_time * spin_speed;
	}
	ubo.projection            = camera.matrices.perspective;
	ubo.view                  = camera.matrices.view;
	ubo.ringRadius            = ring_radius;
	ubo.particleScale         = particle_scale;
	ubo.iparticleSizeInstance  = iparticle_size_instance;
	ubo.ringParticleNum        = particle_count;
	ubo.locationFrequencies     = location_frequencies;
	ubo.timeFrequencies         = time_frequencies;
	ubo.displace                = displace;
	ubo.particlePositionNoise   = particle_position_noise;
	ubo.ringWidth1               = ring_width1;
	ubo.sizeRate                 = size_rate;
	ubo.particleReformNoise      = particle_reform_noise;
	ubo.pointSizeScale            = point_size_scale;
	uniform_buffer->convert_and_update(ubo);
}

void RingParticles::setup_descriptor_set_layout()
{
	std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(
	        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        VK_SHADER_STAGE_VERTEX_BIT,
	        0),
	};
	VkDescriptorSetLayoutCreateInfo info =
	    vkb::initializers::descriptor_set_layout_create_info(
	        set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	VK_CHECK(vkCreateDescriptorSetLayout(get_device().get_handle(), &info, nullptr, &descriptor_set_layout));

	VkPipelineLayoutCreateInfo pl_info =
	    vkb::initializers::pipeline_layout_create_info(&descriptor_set_layout, 1);
	VK_CHECK(vkCreatePipelineLayout(get_device().get_handle(), &pl_info, nullptr, &pipeline_layout));
}

void RingParticles::setup_descriptor_pool()
{
	// Pool shared by the particle pass (UBO) and the post pass (UBO + sampler).
	std::vector<VkDescriptorPoolSize> pool_sizes = {
	    vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
	    vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1),
	};
	VkDescriptorPoolCreateInfo info =
	    vkb::initializers::descriptor_pool_create_info(
	        static_cast<uint32_t>(pool_sizes.size()), pool_sizes.data(), 2);
	VK_CHECK(vkCreateDescriptorPool(get_device().get_handle(), &info, nullptr, &descriptor_pool));
}

void RingParticles::setup_descriptor_set()
{
	VkDescriptorSetAllocateInfo alloc =
	    vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);
	VK_CHECK(vkAllocateDescriptorSets(get_device().get_handle(), &alloc, &descriptor_set));

	VkDescriptorBufferInfo buf_info = create_descriptor(*uniform_buffer);
	std::vector<VkWriteDescriptorSet> writes = {
	    vkb::initializers::write_descriptor_set(
	        descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &buf_info),
	};
	vkUpdateDescriptorSets(get_device().get_handle(),
	                        static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RingParticles::prepare_pipelines()
{
	// Point mode: no vertex input (position is procedural from gl_InstanceIndex).
	// Leave vertex binding/attribute counts at 0.
	VkPipelineVertexInputStateCreateInfo vertex_input =
	    vkb::initializers::pipeline_vertex_input_state_create_info();
	// vertexBindingDescriptionCount = 0, vertexAttributeDescriptionCount = 0

	VkPipelineInputAssemblyStateCreateInfo input_assembly =
	    vkb::initializers::pipeline_input_assembly_state_create_info(
	        VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterization =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

	VkPipelineColorBlendAttachmentState blend =
	    vkb::initializers::pipeline_color_blend_attachment_state(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo color_blend =
	    vkb::initializers::pipeline_color_blend_state_create_info(1, &blend);

	VkPipelineDepthStencilStateCreateInfo depth_stencil =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewport =
	    vkb::initializers::pipeline_viewport_state_create_info(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo multisample =
	    vkb::initializers::pipeline_multisample_state_create_info(VK_SAMPLE_COUNT_1_BIT, 0);

	std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic =
	    vkb::initializers::pipeline_dynamic_state_create_info(dyn.data(), static_cast<uint32_t>(dyn.size()), 0);

	std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
	    load_shader("ring_particles", "particle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
	    load_shader("ring_particles", "particle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	VkGraphicsPipelineCreateInfo info =
	    vkb::initializers::pipeline_create_info(pipeline_layout, offscreen_render_pass, 0);
	info.pVertexInputState   = &vertex_input;
	info.pInputAssemblyState = &input_assembly;
	info.pRasterizationState = &rasterization;
	info.pColorBlendState    = &color_blend;
	info.pDepthStencilState  = &depth_stencil;
	info.pViewportState      = &viewport;
	info.pMultisampleState   = &multisample;
	info.pDynamicState        = &dynamic;
	info.stageCount          = static_cast<uint32_t>(stages.size());
	info.pStages              = stages.data();

	VK_CHECK(vkCreateGraphicsPipelines(get_device().get_handle(), pipeline_cache, 1, &info, nullptr, &pipeline));
}

void RingParticles::build_command_buffers()
{
	VkCommandBufferBeginInfo begin = vkb::initializers::command_buffer_begin_info();

	VkClearValue clears[2];
	clears[0].color        = {{0.025f, 0.025f, 0.035f, 1.0f}};
	clears[1].depthStencil = {1.0f, 0};

	VkExtent2D extent = get_render_context().get_surface_extent();

	// Offscreen pass begin info (renders particles to the Color attachment).
	VkRenderPassBeginInfo off_rp = vkb::initializers::render_pass_begin_info();
	off_rp.renderPass             = offscreen_render_pass;
	off_rp.renderArea.offset.x    = 0;
	off_rp.renderArea.offset.y     = 0;
	off_rp.renderArea.extent       = extent;
	off_rp.clearValueCount         = 2;
	off_rp.pClearValues            = clears;
	off_rp.framebuffer             = offscreen_framebuffer;

	// Swapchain pass begin info (post-process: rotation blur -> swapchain).
	VkRenderPassBeginInfo post_rp = vkb::initializers::render_pass_begin_info();
	post_rp.renderArea.extent     = extent;
	post_rp.clearValueCount       = 1;
	post_rp.pClearValues           = &clears[0];   // swapchain color only (depth not used)

	for (int32_t i = 0; i < static_cast<int32_t>(draw_cmd_buffers.size()); ++i)
	{
		VK_CHECK(vkBeginCommandBuffer(draw_cmd_buffers[i], &begin));

		// --- 1) Offscreen: render particles to Color (final: SHADER_READ_ONLY) ---
		vkCmdBeginRenderPass(draw_cmd_buffers[i], &off_rp, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport vp = vkb::initializers::viewport(static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
		vkCmdSetViewport(draw_cmd_buffers[i], 0, 1, &vp);
		VkRect2D scissor = vkb::initializers::rect2D(extent.width, extent.height, 0, 0);
		vkCmdSetScissor(draw_cmd_buffers[i], 0, 1, &scissor);

		vkCmdBindDescriptorSets(draw_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
		vkCmdBindPipeline(draw_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		// Point mode: 1 vertex per instance, position procedural in vertex shader.
		vkCmdDraw(draw_cmd_buffers[i], 1, particle_count, 0, 0);
		vkCmdEndRenderPass(draw_cmd_buffers[i]);

		// Make the offscreen color write visible to the post fragment-shader read.
		{
			VkImageMemoryBarrier b{};
			b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
			b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
			b.image                 = offscreen_color_image;
			b.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(draw_cmd_buffers[i],
			                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &b);
		}

		// --- 2) Swapchain: rotation-blur post process (Color -> swapchain) ---
		post_rp.renderPass  = render_pass;
		post_rp.framebuffer = framebuffers[i];
		vkCmdBeginRenderPass(draw_cmd_buffers[i], &post_rp, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(draw_cmd_buffers[i], 0, 1, &vp);
		vkCmdSetScissor(draw_cmd_buffers[i], 0, 1, &scissor);

		vkCmdBindDescriptorSets(draw_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        post_pipeline_layout, 0, 1, &post_descriptor_set, 0, nullptr);
		vkCmdBindPipeline(draw_cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline);
		vkCmdDraw(draw_cmd_buffers[i], 3, 1, 0, 0);   // fullscreen triangle

		draw_ui(draw_cmd_buffers[i]);
		vkCmdEndRenderPass(draw_cmd_buffers[i]);

		VK_CHECK(vkEndCommandBuffer(draw_cmd_buffers[i]));
	}
}

void RingParticles::render(float delta_time)
{
	if (!prepared)
	{
		return;
	}
	update_uniform_buffers(delta_time);
	update_post_uniform_buffer();
	ApiVulkanSample::prepare_frame();

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers     = &draw_cmd_buffers[current_buffer];
	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

	ApiVulkanSample::submit_frame();
}

void RingParticles::view_changed()
{
	update_uniform_buffers(0.0f);
}

void RingParticles::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Ring Particles"))
	{
		int32_t count = static_cast<int32_t>(particle_count);
		if (drawer.slider_int("Particle count", &count, 450, 500))
		{
			particle_count = static_cast<uint32_t>(count);
			// Re-record command buffers (instance count changed).
			build_command_buffers();
		}
		drawer.slider_float("Ring radius", &ring_radius, 0.5f, 8.0f);
		drawer.slider_float("Particle scale (offset)", &particle_scale, 0.1f, 3.0f);
		drawer.slider_float("Base size (iparticleSizeInstance)", &iparticle_size_instance, 0.1f, 5.0f);
		drawer.slider_float("PointSize scale", &point_size_scale, 0.0f, 200.0f);
		drawer.checkbox("Spin", &rotate_ring);
		drawer.slider_float("Spin speed", &spin_speed, 0.0f, 2.0f);

		if (drawer.header("Formulas"))
		{
			drawer.text("angle = (id %% N) / N * 2pi + time");
			drawer.text("initPos = (cos, sin, 0)        [unit, no scale]");
			drawer.text("pos.xy = initPos.xy * R * particleScale");
			drawer.text("flow  = time * (1,0,0.3) * timeFreq");
			drawer.text("coord = initPos * locationFreq + flow + seed");
			drawer.text("amp   = displace * particlePositionNoise");
			drawer.text("pos.xyz += perlin(coord + {10086,666,0}) * amp");
			drawer.text("size  = iSizeInst * 0.115 * ringWidth1");
			drawer.text("size  = mix(size*0.5, size, rand(id))");
			drawer.text("size *= perlin(initPos+flow+seed+11223)*5*sizeRate*reform + 1");
			drawer.text("gl_PointSize = clamp(size*scale, 1, 64)");
		}

		if (drawer.header("Noise: position"))
		{
			drawer.slider_float("locationFrequencies", &location_frequencies, 0.05f, 3.0f);
			drawer.slider_float("timeFrequencies", &time_frequencies, 0.0f, 2.0f);
			drawer.slider_float("displace", &displace, 0.0f, 0.5f);
			drawer.slider_float("particlePositionNoise", &particle_position_noise, 0.0f, 1.0f);
		}
		if (drawer.header("Noise: size"))
		{
			drawer.slider_float("ringWidth1", &ring_width1, 0.0f, 2.0f);
			drawer.slider_float("sizeRate", &size_rate, 0.0f, 2.0f);
			drawer.slider_float("particleReformNoise", &particle_reform_noise, 0.0f, 1.0f);
		}
		if (drawer.header("Rotation blur (post)"))
		{
			drawer.checkbox("Enable", &enable_rotation_blur);
			drawer.slider_float("Sweep angle (deg)", &rb_sweep_degrees, 0.0f, 180.0f);
			drawer.slider_int("Samples", reinterpret_cast<int *>(&rb_samples), 1, 64);
			drawer.slider_float("Center X", &rb_center[0], 0.0f, 1.0f);
			drawer.slider_float("Center Y", &rb_center[1], 0.0f, 1.0f);
			drawer.text("pos += perlin(arc over offscreen Color)");
		}
		drawer.text("gl_InstanceIndex -> ring pos + Classic Perlin 3D pos/size");
	}
}

std::unique_ptr<vkb::Application> create_ring_particles()
{
	return std::make_unique<RingParticles>();
}
