# Copyright (c) 2024, Huawei Technologies Co., Ltd.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

# The entry CMakeLists.txt for OpenHarmony
project(ohos_entry)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# Source files — minimal set for OHOS triangle demo
#
# Only includes files with lightweight dependency chains.
# Heavy framework files (platform.cpp, utils.cpp, strings.cpp, drawer.cpp)
# are EXCLUDED because they pull in rendering/, scene_graph/, imgui, etc.
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

    # Utilities — lightweight
    ${FRAMEWORK_DIR}/timer.cpp
    ${FRAMEWORK_DIR}/debug_info.cpp
    # NOTE: drawer.cpp EXCLUDED (depends on imgui)
    # NOTE: fence_pool.cpp EXCLUDED (depends on core/device.h → heavy chain)
    # NOTE: semaphore_pool.cpp EXCLUDED (depends on core/device.h → heavy chain)

    # Common
    # NOTE: strings.cpp includes scene_graph/material.h and core/shader_module.h
    #       but only for type definitions — no link-time deps on those symbols.
    # NOTE: utils.cpp is EXCLUDED (implements functions that call scene_graph/rendering)
    ${FRAMEWORK_DIR}/common/error.cpp
    ${FRAMEWORK_DIR}/common/strings.cpp
    ${FRAMEWORK_DIR}/common/vk_common.cpp
)

set(COMPONENT_SOURCES
    ${COMPONENTS_DIR}/filesystem/src/filesystem.cpp
    ${COMPONENTS_DIR}/filesystem/src/legacy.cpp
    ${COMPONENTS_DIR}/filesystem/src/std_filesystem.cpp
)

set(SAMPLE_SOURCES
    ${ROOT_DIR}/samples/api/ohos_triangle/ohos_triangle.cpp
)

# ---------------------------------------------------------------------------
# NAPI bridge library (the actual .so loaded by OpenHarmony)
# ---------------------------------------------------------------------------
add_library(entry SHARED
    napi_init.cpp
    ${FRAMEWORK_SOURCES}
    ${COMPONENT_SOURCES}
    ${SAMPLE_SOURCES}
)

# Include paths — only what's actually needed
target_include_directories(entry PRIVATE
    ${ROOT_DIR}
    ${ROOT_DIR}/app
    ${ROOT_DIR}/samples/api/ohos_triangle
    ${FRAMEWORK_DIR}
    ${COMPONENTS_DIR}/filesystem/include
    ${COMPONENTS_DIR}/core/include
    ${THIRD_PARTY_DIR}/spdlog/include
    ${THIRD_PARTY_DIR}/stb
    ${THIRD_PARTY_DIR}/vma/include
    ${THIRD_PARTY_DIR}/glm
    ${THIRD_PARTY_DIR}/volk
    ${THIRD_PARTY_DIR}/imgui
)

target_compile_definitions(entry PRIVATE
    VK_USE_PLATFORM_OHOS_KHR
    VK_USE_PLATFORM_OHOS
    OHOS
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
    ${LIB_ACE_NDK}
    ${LIB_NATIVE_WINDOW}
    ${LIB_HILOG}
    ${LIB_ACE_NAPI}
    ${LIB_RAWFILE}
)
