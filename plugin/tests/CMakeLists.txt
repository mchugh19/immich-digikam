cmake_minimum_required(VERSION 3.16)
project(immich-plugin-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

enable_testing()

add_executable(test_plugin_logic test_plugin_logic.cpp)
target_include_directories(test_plugin_logic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_plugin_logic PRIVATE Qt6::Core Qt6::Test)

add_test(NAME plugin_logic COMMAND test_plugin_logic)
