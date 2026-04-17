# Copyright (c) 2024, Huawei Technologies Co., Ltd.
#
# SPDX-License-Identifier: Apache-2.0

get_filename_component(FOLDER_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} PATH)
get_filename_component(CATEGORY_NAME ${PARENT_DIR} NAME)

add_sample(
    ID ${FOLDER_NAME}
    CATEGORY ${CATEGORY_NAME}
    AUTHOR "Huawei"
    NAME "OHOS Triangle"
    DESCRIPTION "A basic triangle demo for OpenHarmony"
    SHADER_FILES_GLSL
        "ohos_triangle/glsl/triangle.vert"
        "ohos_triangle/glsl/triangle.frag"
)
