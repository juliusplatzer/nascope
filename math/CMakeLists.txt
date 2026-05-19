find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(nascope_math STATIC
    core.cpp
    geom.cpp
    latlong.cpp
)

target_include_directories(nascope_math
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_math
    PUBLIC
        Qt6::Core
        Qt6::Gui
)
