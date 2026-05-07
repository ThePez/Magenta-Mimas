# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mini-project-base)

target_sources(app PRIVATE 
    src/main.c
    src/base_gatt.c
    src/localisation.c
    src/rb_tree.c
    src/ble_shell.c
    src/observer.c
    src/uart.c
    src/json.c
    src/matrix.c
    src/kalman.c
)
