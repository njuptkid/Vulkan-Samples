# Copyright (c) 2024, Huawei Technologies Co., Ltd.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

# The entry CMakeLists.txt for OpenHarmony
project(ohos_entry)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library -Wall -Wextra -Werror=return-type")

# Define the root of the Vulkan-Samples project
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(THIRD_PARTY_DIR ${ROOT_DIR}/third_party)
set(FRAMEWORK_DIR ${ROOT_DIR}/framework)
set(COMPONENTS_DIR ${ROOT_DIR}/components)

# ---------------------------------------------------------------------------
# Vulkan headers (header-only interface target)
# ---------------------------------------------------------------------------
add_library(vulkan_headers INTERFACE)
target_include_directories(vulkan_headers SYSTEM INTERFACE
    ${THIRD_PARTY_DIR}/vulkan/include
)
target_compile_definitions(vulkan_headers INTERFACE
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_OHOS_KHR
)

# ---------------------------------------------------------------------------
# volk (Vulkan loader)
# ---------------------------------------------------------------------------
set(VOLK_STATIC_DEFINES "VK_NO_PROTOTYPES;VK_USE_PLATFORM_OHOS_KHR" CACHE STRING "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/volk ${CMAKE_CURRENT_BINARY_DIR}/volk)
target_link_libraries(volk PUBLIC vulkan_headers)

# ---------------------------------------------------------------------------
# spdlog (header-only logging library)
# ---------------------------------------------------------------------------
set(SPDLOG_COMPILED_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/spdlog ${CMAKE_CURRENT_BINARY_DIR}/spdlog)

# ---------------------------------------------------------------------------
# fmt (formatting library, required by spdlog macros)
# ---------------------------------------------------------------------------
set(FMT_MASTER_PROJECT OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/fmt ${CMAKE_CURRENT_BINARY_DIR}/fmt)

# ---------------------------------------------------------------------------
# VMA (Vulkan Memory Allocator - header only, needed by vk_common.h)
# ---------------------------------------------------------------------------
add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ${THIRD_PARTY_DIR}/vma/include)

# ---------------------------------------------------------------------------
# GLM (math library, header only, needed by helpers.h/glm_common.h)
# ---------------------------------------------------------------------------
add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE ${THIRD_PARTY_DIR}/glm)

# ---------------------------------------------------------------------------
# stb (for stb_image_write used by filesystem/legacy.cpp)
# ---------------------------------------------------------------------------
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${THIRD_PARTY_DIR}/stb)

# ---------------------------------------------------------------------------
# SPIRV-Cross (for spirv_reflection.h used by shader_module)
# ---------------------------------------------------------------------------
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/spirv-cross ${CMAKE_CURRENT_BINARY_DIR}/spirv-cross)

# ---------------------------------------------------------------------------
# libktx (KTX texture loading — needed by scene_graph/components/image/ktx.cpp)
# ---------------------------------------------------------------------------
set(KTX_DIR ${THIRD_PARTY_DIR}/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/external/dfdutils/createdfd.c
    ${KTX_DIR}/external/dfdutils/colourspaces.c
    ${KTX_DIR}/external/dfdutils/interpretdfd.c
    ${KTX_DIR}/external/dfdutils/printdfd.c
    ${KTX_DIR}/external/dfdutils/queries.c
    ${KTX_DIR}/external/dfdutils/vk2dfd.c
    ${KTX_DIR}/external/etcdec/etcdec.cxx
    ${KTX_DIR}/lib/etcunpack.cxx
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/info.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/strings.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/basis_transcode.cpp
    ${KTX_DIR}/lib/miniz_wrapper.cpp
    ${KTX_DIR}/external/basisu/transcoder/basisu_transcoder.cpp
    ${KTX_DIR}/external/basisu/zstd/zstd.c
    ${KTX_DIR}/lib/texture1.c
    ${KTX_DIR}/lib/texture2.c
    ${KTX_DIR}/lib/vkformat_check.c
    ${KTX_DIR}/lib/vkformat_str.c
    ${KTX_DIR}/lib/vkformat_typesize.c
    ${KTX_DIR}/lib/vkformat_check_variant.c
    ${KTX_DIR}/lib/vk_funcs.c
    ${KTX_DIR}/lib/vkloader.c
)
set(KTX_INCLUDE_DIRS
    ${KTX_DIR}/include
    ${KTX_DIR}/lib
    ${KTX_DIR}/utils
    ${KTX_DIR}/external
    ${KTX_DIR}/external/basisu/zstd
    ${KTX_DIR}/external/basisu/transcoder
    ${KTX_DIR}/other_include
)
add_library(ktx STATIC ${KTX_SOURCES})
target_compile_definitions(ktx PUBLIC LIBKTX KTX_FEATURE_WRITE=0 BASISD_SUPPORT_KTX2_ZSTD=0 BASISU_NO_ITERATOR_DEBUG_LEVEL)
target_include_directories(ktx SYSTEM PUBLIC ${KTX_INCLUDE_DIRS})
target_link_libraries(ktx PUBLIC vulkan_headers)

# ---------------------------------------------------------------------------
# astc-encoder (ASTC texture decoding — needed by scene_graph/components/image/astc.cpp)
# ---------------------------------------------------------------------------
set(ASTCENC_ISA_NEON ON)
set(ASTCENC_ISA_NATIVE OFF)
set(ASTCENC_ISA_NONE OFF)
set(ASTCENC_ISA_SSE41 OFF)
set(ASTCENC_ISA_SSE2 OFF)
set(ASTCENC_ISA_AVX2 OFF)
set(ASTCENC_CLI OFF)
set(ASTCENC_UNITTEST OFF)
set(ASTCENC_DECOMPRESSOR ON)
set(ASTCENC_UNIVERSAL_BUILD OFF)
add_subdirectory(${THIRD_PARTY_DIR}/astc ${CMAKE_CURRENT_BINARY_DIR}/astc)
# Suppress OHOS toolchain flag warnings in astc build
target_compile_options(astcdec-neon-static PRIVATE -Wno-unused-command-line-argument)

# ---------------------------------------------------------------------------
# imgui core (needed by gui.h which is included by vulkan_sample.h;
#             GUI is never created at runtime — gui pointer stays null)
# ---------------------------------------------------------------------------
set(IMGUI_SOURCES
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
)

# ---------------------------------------------------------------------------
# Source files for VulkanSample-based OHOS triangle demo
#
# Uses VulkanSampleCpp template which requires: RenderContext (template-only),
# Stats, and gui.h compilation deps (imgui core + drawer).
# utils.cpp is still EXCLUDED (depends on scene_graph).
# ---------------------------------------------------------------------------

set(FRAMEWORK_SOURCES
    # Platform — only OHOS-specific
    # NOTE: window.cpp is NOT compiled; Window base methods are inlined
    #       in ohos_window.cpp to avoid the platform.h heavy dependency chain.
    ${FRAMEWORK_DIR}/platform/ohos/ohos_platform.cpp
    ${FRAMEWORK_DIR}/platform/ohos/ohos_window.cpp
    ${FRAMEWORK_DIR}/platform/application.cpp
    ${FRAMEWORK_DIR}/platform/configuration.cpp
    ${FRAMEWORK_DIR}/platform/input_events.cpp

    # Utilities
    ${FRAMEWORK_DIR}/timer.cpp
    ${FRAMEWORK_DIR}/debug_info.cpp
    ${FRAMEWORK_DIR}/drawer.cpp
    ${FRAMEWORK_DIR}/fence_pool.cpp
    ${FRAMEWORK_DIR}/semaphore_pool.cpp
    ${FRAMEWORK_DIR}/resource_cache.cpp
    ${FRAMEWORK_DIR}/resource_binding_state.cpp
    ${FRAMEWORK_DIR}/resource_record.cpp
    ${FRAMEWORK_DIR}/resource_replay.cpp
    ${FRAMEWORK_DIR}/hpp_resource_cache.cpp
    ${FRAMEWORK_DIR}/spirv_reflection.cpp

    # Stats — required by VulkanSample::prepare()
    ${FRAMEWORK_DIR}/stats/frame_time_stats_provider.cpp
    ${FRAMEWORK_DIR}/stats/stats.cpp
    ${FRAMEWORK_DIR}/stats/stats_provider.cpp
    ${FRAMEWORK_DIR}/stats/vulkan_stats_provider.cpp

    # Scene graph — referenced by VulkanSample template methods (dead code paths
    # but linker still needs symbols).  Exclude ktx/astc (external libs not available).
    ${FRAMEWORK_DIR}/scene_graph/component.cpp
    ${FRAMEWORK_DIR}/scene_graph/scene.cpp
    ${FRAMEWORK_DIR}/scene_graph/script.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/aabb.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/camera.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/hpp_image.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/image.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/image/stb.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/light.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/material.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/mesh.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/orthographic_camera.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/pbr_material.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/perspective_camera.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/sub_mesh.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/texture.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/transform.cpp
    ${FRAMEWORK_DIR}/scene_graph/scripts/animation.cpp
    ${FRAMEWORK_DIR}/scene_graph/scripts/free_camera.cpp
    ${FRAMEWORK_DIR}/scene_graph/scripts/node_animation.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/image/ktx.cpp
    ${FRAMEWORK_DIR}/scene_graph/components/image/astc.cpp

    # Common (utils.cpp now included — scene_graph deps are resolved above)
    ${FRAMEWORK_DIR}/common/utils.cpp
    ${FRAMEWORK_DIR}/common/error.cpp
    ${FRAMEWORK_DIR}/common/strings.cpp
    ${FRAMEWORK_DIR}/common/vk_common.cpp

    # Core — framework Vulkan pipeline wrappers
    ${FRAMEWORK_DIR}/core/acceleration_structure.cpp
    ${FRAMEWORK_DIR}/core/allocated.cpp
    ${FRAMEWORK_DIR}/core/command_pool_base.cpp
    ${FRAMEWORK_DIR}/core/debug.cpp
    ${FRAMEWORK_DIR}/core/descriptor_pool.cpp
    ${FRAMEWORK_DIR}/core/descriptor_set.cpp
    ${FRAMEWORK_DIR}/core/descriptor_set_layout.cpp
    ${FRAMEWORK_DIR}/core/framebuffer.cpp
    ${FRAMEWORK_DIR}/core/hpp_debug.cpp
    ${FRAMEWORK_DIR}/core/hpp_image_core.cpp
    ${FRAMEWORK_DIR}/core/hpp_image_view.cpp
    ${FRAMEWORK_DIR}/core/hpp_pipeline_layout.cpp
    ${FRAMEWORK_DIR}/core/hpp_queue.cpp
    ${FRAMEWORK_DIR}/core/hpp_sampler.cpp
    ${FRAMEWORK_DIR}/core/hpp_swapchain.cpp
    ${FRAMEWORK_DIR}/core/image_core.cpp
    ${FRAMEWORK_DIR}/core/image_view.cpp
    ${FRAMEWORK_DIR}/core/pipeline.cpp
    ${FRAMEWORK_DIR}/core/pipeline_layout.cpp
    ${FRAMEWORK_DIR}/core/query_pool.cpp
    ${FRAMEWORK_DIR}/core/queue.cpp
    ${FRAMEWORK_DIR}/core/render_pass.cpp
    ${FRAMEWORK_DIR}/core/sampler_core.cpp
    ${FRAMEWORK_DIR}/core/sampled_image.cpp
    ${FRAMEWORK_DIR}/core/shader_module.cpp
    ${FRAMEWORK_DIR}/core/swapchain.cpp

    # Rendering — pipeline state
    ${FRAMEWORK_DIR}/rendering/pipeline_state.cpp
)

set(COMPONENT_SOURCES
    ${COMPONENTS_DIR}/filesystem/src/filesystem.cpp
    ${COMPONENTS_DIR}/filesystem/src/legacy.cpp
    ${COMPONENTS_DIR}/filesystem/src/std_filesystem.cpp
)

set(SAMPLE_SOURCES
    ${ROOT_DIR}/samples/api/ohos_triangle/ohos_triangle.cpp
    ${ROOT_DIR}/samples/api/ohos_triangle/vma_impl.cpp
    ${ROOT_DIR}/samples/api/ohos_triangle/vulkan_hpp_dispatch.cpp
)

# ---------------------------------------------------------------------------
# NAPI bridge library (the actual .so loaded by OpenHarmony)
# ---------------------------------------------------------------------------
add_library(entry SHARED
    napi_init.cpp
    ${FRAMEWORK_SOURCES}
    ${IMGUI_SOURCES}
    ${COMPONENT_SOURCES}
    ${SAMPLE_SOURCES}
)

# Include paths
target_include_directories(entry PRIVATE
    ${ROOT_DIR}
    ${ROOT_DIR}/app
    ${ROOT_DIR}/samples/api/ohos_triangle
    ${FRAMEWORK_DIR}
    ${FRAMEWORK_DIR}/rendering
    ${COMPONENTS_DIR}/filesystem/include
    ${COMPONENTS_DIR}/core/include
    ${THIRD_PARTY_DIR}/spdlog/include
    ${THIRD_PARTY_DIR}/stb
    ${THIRD_PARTY_DIR}/vma/include
    ${THIRD_PARTY_DIR}/glm
    ${THIRD_PARTY_DIR}/volk
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/spirv-cross
    ${THIRD_PARTY_DIR}/tinygltf
)

target_compile_definitions(entry PRIVATE
    VK_USE_PLATFORM_OHOS_KHR
    VK_USE_PLATFORM_OHOS
    OHOS
    VKB_VALIDATION_LAYERS
)

# ---------------------------------------------------------------------------
# Locate OHOS NDK system libraries
# ---------------------------------------------------------------------------
find_library(LIB_ACE_NDK      ace_ndk.z)
find_library(LIB_NATIVE_WINDOW native_window)
find_library(LIB_HILOG        hilog_ndk.z)
find_library(LIB_ACE_NAPI     ace_napi.z)
find_library(LIB_RAWFILE      rawfile.z)

if(NOT LIB_ACE_NDK)
    message(FATAL_ERROR "libace_ndk.z not found. Is the OHOS NDK sysroot configured?")
endif()

target_link_libraries(entry PRIVATE
    volk
    spdlog::spdlog_header_only
    fmt::fmt
    spirv-cross-glsl
    ktx
    astcdec-neon-static
    ${LIB_ACE_NDK}
    ${LIB_NATIVE_WINDOW}
    ${LIB_HILOG}
    ${LIB_ACE_NAPI}
    ${LIB_RAWFILE}
)
