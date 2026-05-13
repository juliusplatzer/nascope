find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(nascope_utils STATIC
    resources.cpp
    math.cpp
)

target_include_directories(nascope_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_utils
    PUBLIC
        Qt6::Core
        Qt6::Gui
)
