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
# ohos_triangle (self-contained Vulkan renderer)
# ---------------------------------------------------------------------------
add_subdirectory(${ROOT_DIR}/samples/api/ohos_triangle ${CMAKE_CURRENT_BINARY_DIR}/ohos_triangle)

# ---------------------------------------------------------------------------
# NAPI bridge library (the actual .so loaded by OpenHarmony)
# ---------------------------------------------------------------------------
add_library(entry SHARED napi_init.cpp)

target_include_directories(entry PRIVATE
    ${ROOT_DIR}/samples/api/ohos_triangle
)

target_compile_definitions(entry PRIVATE
    VK_USE_PLATFORM_OHOS_KHR
    VK_USE_PLATFORM_OHOS     # vulkan.h checks this to include vulkan_ohos.h
    OHOS
)


# ---------------------------------------------------------------------------
# Locate OHOS NDK system libraries
# The OHOS NDK sysroot is set by the DevEco CMake toolchain; find_library
# searches it automatically. Use short names (no "lib" prefix, no ".so").
# ---------------------------------------------------------------------------
find_library(LIB_ACE_NDK      ace_ndk.z)       # napi_module_register, etc.
find_library(LIB_NATIVE_WINDOW native_window)   # OHNativeWindow APIs
find_library(LIB_HILOG        hilog_ndk.z)      # OH_LOG_Print
find_library(LIB_ACE_NAPI     ace_napi.z)       # napi core APIs

if(NOT LIB_ACE_NDK)
    message(FATAL_ERROR "libace_ndk.z not found. Is the OHOS NDK sysroot configured?")
endif()
target_link_libraries(entry PRIVATE
    ohos_triangle
    ${LIB_ACE_NDK}
    ${LIB_NATIVE_WINDOW}
    ${LIB_HILOG}
    ${LIB_ACE_NAPI}
)
