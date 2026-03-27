# Copyright (c) 2025, Contributors
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

add_sample(
    ID ${FOLDER_NAME}
    CATEGORY ${CATEGORY_NAME}
    AUTHOR "User"
    NAME "Image Post Processing"
    DESCRIPTION "Demonstrates multi-pass image post-processing with gradient visualization"
    FILES
        ${FOLDER_NAME}.h
        ${FOLDER_NAME}.cpp
    SHADER_FILES_GLSL
        "image_postprocessing/glsl/postprocessing.vert"
        "image_postprocessing/glsl/passthrough.frag"
        "image_postprocessing/glsl/grayscale.frag"
        "image_postprocessing/glsl/gradient.frag"
        "image_postprocessing/glsl/blur.frag"
        "image_postprocessing/glsl/vignette.frag"
)
