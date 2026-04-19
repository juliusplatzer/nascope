cmake_minimum_required(VERSION 3.16)
project(nascope_menu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(menu menu.cpp resources.qrc)
target_link_libraries(menu PRIVATE Qt6::Widgets)
