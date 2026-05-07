cmake_minimum_required(VERSION 3.16)
project(nascope_menu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../renderer ${CMAKE_CURRENT_BINARY_DIR}/renderer)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../asdex ${CMAKE_CURRENT_BINARY_DIR}/asdex)

add_executable(menu menu.cpp resources.qrc)
add_dependencies(menu asdex_scope)
target_link_libraries(menu PRIVATE Qt6::Widgets)
