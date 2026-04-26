# Copyright (c) 2024, Huawei Technologies Co., Ltd.
#
# SPDX-License-Identifier: Apache-2.0

get_filename_component(FOLDER_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} PATH)
get_filename_component(CATEGORY_NAME ${PARENT_DIR} NAME)

set(OHOS_TRIANGLE_EXTRA_SOURCES "")
if(OHOS)
    list(APPEND OHOS_TRIANGLE_EXTRA_SOURCES
        vma_impl.cpp
        vulkan_hpp_dispatch.cpp
    )
endif()

add_sample(
    ID ${FOLDER_NAME}
    CATEGORY ${CATEGORY_NAME}
    AUTHOR "Huawei"
    NAME "OHOS Triangle"
    DESCRIPTION "GPU-driven particle fountain with 3D Euler fluid solver"
    SHADER_FILES_GLSL
        "fluid_particles/glsl/particle.vert"
        "fluid_particles/glsl/particle.frag"
        "fluid_particles/glsl/particle_init.comp"
        "fluid_particles/glsl/particle_update.comp"
        "fluid_particles/glsl/fluid_inject.comp"
        "fluid_particles/glsl/fluid_advect.comp"
        "fluid_particles/glsl/fluid_boundary.comp"
        "fluid_particles/glsl/fluid_boundary_scalar.comp"
        "fluid_particles/glsl/fluid_divergence.comp"
        "fluid_particles/glsl/fluid_pressure.comp"
        "fluid_particles/glsl/fluid_gradient_subtract.comp"
        "fluid_particles/glsl/fluid_clear.comp"
        "fluid_particles/glsl/fluid_vorticity.comp"
        "fluid_particles/glsl/fluid_vorticity_confinement.comp"
        "ohos_triangle/glsl/postprocessing.vert"
        "ohos_triangle/glsl/brightpass.frag"
        "ohos_triangle/glsl/blur_h.frag"
        "ohos_triangle/glsl/blur_v.frag"
        "ohos_triangle/glsl/composite.frag"
    FILES
        ${OHOS_TRIANGLE_EXTRA_SOURCES}
)
