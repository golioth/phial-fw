# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Pull in the shared shell/log Kconfig before find_package(Zephyr).
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../../conf/shell-common.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_test)

target_sources(app PRIVATE
    src/main.c
    src/tilt.c
    src/env.c
)
