find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(nascope_radar STATIC
    tools.cpp
)

target_include_directories(nascope_radar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_radar
    PUBLIC
        Qt6::Core
        Qt6::Gui
        nascope_math
)
