# ARMOR-SOLAR host tests: the protocol library, built with any C++17 compiler.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.16)
project(armor_solar_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_executable(test_solar test_solar.cpp)
add_executable(test_node test_node.cpp)
# the s3-eth board profile (the Waveshare ESP32-S3-ETH): the same core headers compiled for the other board
add_executable(test_board_eth test_board_eth.cpp)
target_compile_definitions(test_board_eth PRIVATE ARMOR_BOARD_S3_ETH=1)
add_executable(emit_samples emit_samples.cpp)
add_executable(emit_poller_samples emit_poller_samples.cpp)

enable_testing()
add_test(NAME solar COMMAND test_solar)
add_test(NAME node COMMAND test_node)
add_test(NAME board_eth COMMAND test_board_eth)
