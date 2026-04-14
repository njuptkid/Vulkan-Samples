# Copyright (c) 2019-2026, Sascha Willems
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 the "License";
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

get_filename_component(FOLDER_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} PATH)
get_filename_component(CATEGORY_NAME ${PARENT_DIR} NAME)

add_sample_with_tags(
    ID ${FOLDER_NAME}
    CATEGORY ${CATEGORY_NAME}
    AUTHOR "Sascha Willems"
    NAME "GPU Particle Effects"
    DESCRIPTION "GPU-driven particle system using compute shaders for emission, physics and lifecycle management"
    SHADER_FILES_GLSL
        "gpu_particle_effects/glsl/particle.vert"
        "gpu_particle_effects/glsl/particle.frag"
        "gpu_particle_effects/glsl/particle_emit.comp"
        "gpu_particle_effects/glsl/particle_update.comp"
    SHADER_FILES_HLSL
        "gpu_particle_effects/hlsl/particle.vert.hlsl"
        "gpu_particle_effects/hlsl/particle.frag.hlsl"
        "gpu_particle_effects/hlsl/particle_emit.comp.hlsl"
        "gpu_particle_effects/hlsl/particle_update.comp.hlsl"
    SHADER_FILES_SLANG
        "gpu_particle_effects/slang/particle.vert.slang"
        "gpu_particle_effects/slang/particle.frag.slang"
        "gpu_particle_effects/slang/particle_emit.comp.slang"
        "gpu_particle_effects/slang/particle_update.comp.slang")
