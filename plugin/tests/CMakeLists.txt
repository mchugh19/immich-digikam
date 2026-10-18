#
# SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

cmake_minimum_required(VERSION 3.16)
project(immich-plugin-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
find_package(KF6Config REQUIRED)

enable_testing()

add_executable(test_plugin_logic test_plugin_logic.cpp)
target_include_directories(test_plugin_logic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_plugin_logic PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME plugin_logic COMMAND test_plugin_logic)

add_executable(test_talker_contract
    test_talker_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../immichtalker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../immichsession.cpp)
target_include_directories(test_talker_contract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_talker_contract PRIVATE
    Qt6::Core Qt6::Network Qt6::Test KF6::ConfigCore)

add_test(NAME talker_contract COMMAND test_talker_contract)
