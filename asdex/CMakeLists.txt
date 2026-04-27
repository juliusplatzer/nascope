cmake_minimum_required(VERSION 3.16)
project(nascope_scope CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebSockets)
find_package(ZLIB REQUIRED)

add_executable(scope
    main.cpp
    scope.cpp
    videomaps.cpp
    maths.cpp
    cursors.cpp
    font.cpp
    lists.cpp
    targets.cpp
    tgtcache.cpp
    dcb.cpp
)
target_link_libraries(scope PRIVATE Qt6::Widgets Qt6::WebSockets ZLIB::ZLIB)
