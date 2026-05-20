# Copyright (c) 2025, Contributors
#
# SPDX-License-Identifier: Apache-2.0

get_filename_component(FOLDER_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} PATH)
get_filename_component(CATEGORY_NAME ${PARENT_DIR} NAME)

add_sample(
    ID ${FOLDER_NAME}
    CATEGORY ${CATEGORY_NAME}
    AUTHOR "Contributors"
    NAME "Vortex Ring"
    DESCRIPTION "Vortex particle simulation demonstrating vortex ring dynamics using compute shaders"
    SHADER_FILES_GLSL
        # === Core FFT-VIC pipeline ===
        "vortex_ring/glsl/vortex_init.comp"
        "vortex_ring/glsl/vorton_p2g.comp"
        "vortex_ring/glsl/fft_3d.comp"
        "vortex_ring/glsl/spectral_solver.comp"
        "vortex_ring/glsl/vorton_update_grid.comp"
        "vortex_ring/glsl/jacobian_grid.comp"
        # === Tracer pipeline ===
        "vortex_ring/glsl/tracer_emit.comp"
        "vortex_ring/glsl/tracer_advect.comp"
        "vortex_ring/glsl/tracer_compact.comp"
        # === Rendering ===
        "vortex_ring/glsl/tracer.vert"
        "vortex_ring/glsl/tracer.frag"
        "vortex_ring/glsl/particle_copy.vert"
        "vortex_ring/glsl/particle_copy.frag"
        # === Alternative methods (implemented but not enabled) ===
        "vortex_ring/glsl/velocity_compute.comp"
        "vortex_ring/glsl/velocity_jacobian.comp"
        "vortex_ring/glsl/vorton_update.comp"
        "vortex_ring/glsl/spatial_hash.comp"
        "vortex_ring/glsl/cell_count.comp"
        "vortex_ring/glsl/prefix_sum.comp"
        "vortex_ring/glsl/scatter.comp"
        "vortex_ring/glsl/velocity_grid_hash.comp"
        "vortex_ring/glsl/velocity_grid_direct.comp"
        "vortex_ring/glsl/diffuse_pse_bidirectional.comp")