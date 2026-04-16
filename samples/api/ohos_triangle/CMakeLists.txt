# Copyright (c) 2024, Huawei Technologies Co., Ltd.
#
# SPDX-License-Identifier: Apache-2.0

# This sample is self-contained: it does NOT inherit from any framework base
# class and does NOT depend on the heavy Vulkan-Samples framework. It uses
# volk directly for Vulkan dispatch and OHNativeWindow for surface creation.

add_library(ohos_triangle STATIC
    ohos_triangle.cpp
    ohos_triangle.h
)

find_library(LIB_HILOG hilog_ndk.z)

target_link_libraries(ohos_triangle PUBLIC
    volk
    ${LIB_HILOG}
)

target_include_directories(ohos_triangle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(ohos_triangle PROPERTIES FOLDER "Samples/OHOS")
