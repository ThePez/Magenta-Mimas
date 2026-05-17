# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../drivers)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

project(project-helm)

if(DEFINED ID)
    zephyr_compile_definitions(NODE_ID=${ID})
endif()

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE
    ${app_sources}
)
